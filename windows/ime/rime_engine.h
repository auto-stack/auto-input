// AutoIME - narrow librime adapter (PLAN-069 T-03, PLAN-070 T-04).
// Wraps session create / key press / context snapshot / commit / destroy and
// owns resource deployment: the shipped resources are copied into AutoIME's
// private user dir, compiled there, and validated before the IME reports
// ready. A failed new resource set falls back to the last known-good set, then
// to the Luna schema, then to keyboard pass-through.
//
// The DLL starts initialization on a worker thread so the first key press never
// waits for dictionary compilation; until ready every key passes through.
#ifndef AUTOIME_RIME_ENGINE_H_
#define AUTOIME_RIME_ENGINE_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include "../engine/engine_client.h"
#include "../probe/rime_api.h"

class RimeEngine {
 public:
  RimeEngine() = default;
  ~RimeEngine();
  RimeEngine(const RimeEngine&) = delete;
  RimeEngine& operator=(const RimeEngine&) = delete;

  struct Snapshot {
    std::string preedit_utf8;       // composition preedit
    std::wstring preedit_utf16;     // same text for the candidate window
    int preedit_cursor = 0;         // UTF-16 units, from the engine cursor
    int preedit_sel_start = 0;      // UTF-16; == cursor when no selection
    int preedit_sel_end = 0;
    bool is_composing = false;
    // Candidates of the CURRENT page only, with the engine's real labels.
    std::vector<std::wstring> candidates;  // UTF-16 for GDI (current page)
    std::vector<std::wstring> labels;      // parallel to candidates
    std::vector<std::wstring> comments;    // parallel to candidates
    int highlighted = 0;                   // page-relative
    int page_no = 0;
    bool is_last_page = true;
    std::string select_keys;
    uint64_t version = 0;  // bumps per snapshot; stale clicks compare
    std::string commit_preview_utf8;
  };

  // Directory of the AutoIME DLL/EXE (used to locate <dir>\resources).
  void SetModuleDir(const std::string& module_dir) { module_dir_ = module_dir; }
  // PLAN-078 T-03: shared-engine client mode. The per-user autoime_engine.exe
  // owns librime and the user dictionary; this adapter forwards every session
  // operation over the local pipe and never loads rime.dll or opens the user
  // database. EnsureReady/StartAsync then mean "connect, or start the owner and
  // connect"; a failed owner leaves the engine not-ready (keyboard pass-through)
  // and is retried on the next attempt, never falling back to an in-process
  // dictionary owner.
  void SetRemote(bool remote) { remote_ = remote; }
  bool remote() const { return remote_; }
  // Owner executable override (default <module_dir>\autoime_engine.exe).
  void SetEngineExe(const std::string& path) { engine_exe_override_ = path; }
  // Instance suffix for the per-user pipe/mutex (isolated tests and staged
  // upgrades). Empty means the default owner.
  void SetRemoteInstance(const std::string& instance) {
    remote_instance_ = instance;
  }
  // Gracefully stops the connected owner (install/upgrade, tests). Other hosts
  // reconnect to the next owner; learnings are finalized first.
  bool ShutdownOwner(std::string* error);
  // PLAN-078 T-02 confirmation contract, forwarded to the owner.
  bool ConfirmCommit(uintptr_t session);
  bool ForgetCommit(uintptr_t session, const std::string& text_utf8);
  // Explicit overrides (headless deploy tool / tests).
  void SetPaths(const std::string& resources_dir,
                const std::string& user_data_dir) {
    resources_override_ = resources_dir;
    user_override_ = user_data_dir;
  }

  // Loads rime.dll, deploys and initializes once. Safe to call repeatedly;
  // returns false with a diagnosable message when the engine is unusable.
  bool EnsureReady(std::string* error);
  // Re-runs resource deployment in this process (owner-side reload). Only used
  // by the owner; sessions are discarded.
  bool Redeploy(std::string* error);
  // Initialization/deployment on a worker thread (TSF activation path).
  void StartAsync();
  // Joins the worker and finalizes the engine (process teardown).
  void Shutdown();

  bool ready() const { return ready_.load(); }
  bool disabled() const { return disabled_.load(); }
  bool fallback() const { return fallback_.load(); }
  bool deploying() const { return started_.load() && !finished_.load(); }
  const std::string& deployment_version() const { return deployment_version_; }
  const std::string& deployment_detail() const { return deployment_detail_; }


  const std::string& dll_path() const { return dll_path_; }
  const std::string& user_data_dir() const { return user_data_dir_; }
  const std::string& resources_dir() const { return resources_dir_; }
  const std::string& schema() const { return schema_; }

  uintptr_t CreateSession();
  void DestroySession(uintptr_t session);

  // Sends one key with its modifier mask; returns true when Rime consumed it.
  bool ProcessKey(uintptr_t session, int keysym, int modifiers);
  // Reads the composition context; false when the engine call fails.
  bool GetContext(uintptr_t session, Snapshot* out);
  // Reports schema mode flags (ascii/disabled). False when unavailable.
  bool GetStatus(uintptr_t session, bool* ascii_mode, bool* disabled);
  // Runtime option switch (e.g. "ascii_mode"); no-op when the API is old.
  void SetOption(uintptr_t session, const char* option, bool value);
  // Returns the pending commit text ("" when none) and consumes it.
  std::string TakeCommit(uintptr_t session, bool* has_commit);
  void ClearComposition(uintptr_t session);
  // Selects by PAGE-LOCAL index (candidate window click).
  bool SelectCandidateOnPage(uintptr_t session, size_t page_index);
  void SelectCandidate(uintptr_t session, size_t index);
  // Removes the user-dictionary entry behind a page candidate (PLAN-078 T-02:
  // the compensating action when the host failed to insert an already
  // committed text). False when librime has no such entry or API.
  bool DeleteCandidateOnPage(uintptr_t session, size_t page_index);

 private:
  bool LoadDll(std::string* error);
  void WriteDefaultCustomYaml() const;
  bool InitializeAndMaintain(bool full_check);
  bool ValidateSchema(const std::string& expected, std::string* actual);
  void FinalizeEngine();
  void CommitResources();
  bool RollbackResources();
  static std::string EnvOr(const char* env, const std::string& fallback);

  // Shared-engine client helpers (PLAN-078 T-03).
  bool EnsureRemoteReady(std::string* error);
  bool ResolvePaths(std::string* error);
  bool ResolveOwnerExe(std::string* path);
  bool StartOwner(const std::string& exe, std::string* error);
  bool RemoteFailed(const std::string& what, const std::string& error);

  std::mutex init_mutex_;
  std::atomic<bool> ready_{false};
  std::atomic<bool> disabled_{false};
  std::atomic<bool> fallback_{false};
  std::atomic<bool> started_{false};
  std::atomic<bool> finished_{false};
  std::thread worker_;

  RimeApi* api_ = nullptr;
  HMODULE module_ = nullptr;
  std::string dll_path_;
  std::string shared_data_dir_;
  std::string user_data_dir_;
  std::string resources_dir_;
  std::string module_dir_;
  std::string resources_override_;
  std::string user_override_;
  std::string schema_ = "luna_pinyin_simp";
  std::string schema_override_;
  std::string deployment_version_;
  std::string deployment_detail_;
  // Shared-engine state (PLAN-078 T-03).
  bool remote_ = false;
  std::string engine_exe_override_;
  std::string remote_instance_;
  std::string owner_exe_;
  std::unique_ptr<autoime::EngineClient> client_;
  std::mutex start_mutex_;
  std::mutex client_mutex_;
};

#endif  // AUTOIME_RIME_ENGINE_H_
