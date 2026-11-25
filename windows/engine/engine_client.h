// AutoIME shared-engine client (PLAN-078 T-02 prototype, wired into the TSF
// DLL by T-03). Owns the pipe connection and speaks the protocol in
// protocol.h. No librime dependency: a client never loads rime.dll or opens
// the user dictionary.
#ifndef AUTOIME_ENGINE_CLIENT_H_
#define AUTOIME_ENGINE_CLIENT_H_

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

#include "protocol.h"

namespace autoime {

// Mirror of RimeEngine::Snapshot over the wire. Keeping it separate lets the
// prototype and the automated harness compile without the engine adapter.
struct RemoteSnapshot {
  std::string preedit_utf8;
  std::wstring preedit_utf16;
  int preedit_cursor = 0;
  int preedit_sel_start = 0;
  int preedit_sel_end = 0;
  bool is_composing = false;
  std::vector<std::wstring> candidates;
  std::vector<std::wstring> labels;
  std::vector<std::wstring> comments;
  int highlighted = 0;
  int page_no = 0;
  bool is_last_page = true;
  // False when the owner no longer holds this session (redeploy/restart).
  bool session_ok = true;
  std::string select_keys;
  uint64_t version = 0;
  std::string commit_preview_utf8;
};

// Per-user object names: the pipe and the singleton mutex are derived from the
// current token's user SID, so one interactive user cannot connect to (or
// replace) another user's engine.
bool CurrentUserSidString(std::string* sid, std::string* error);
bool CurrentUserPipeName(std::wstring* name, std::string* error);
bool CurrentUserMutexName(std::wstring* name, std::string* error);
// Per-user names with an explicit instance suffix (tests, side-by-side
// versions). An empty instance is the default owner.
bool UserPipeName(const std::string& instance, std::wstring* name,
                  std::string* error);
bool UserMutexName(const std::string& instance, std::wstring* name,
                   std::string* error);
// PLAN-078 T-03 follow-up: one owner per USER DATA ROOT, not just per user.
// The default root (%LOCALAPPDATA%\AutoIME\rime) keeps the un-suffixed owner
// (compatible with existing installs); any other root gets a deterministic
// "profile-<hash>" suffix so isolated/alternate profiles never share a
// dictionary or learning with the default profile.
bool ProfileInstanceForUserDir(const std::string& user_dir,
                               std::string* instance, std::string* error);

class EngineClient {
 public:
  EngineClient() = default;
  ~EngineClient();
  EngineClient(const EngineClient&) = delete;
  EngineClient& operator=(const EngineClient&) = delete;

  // Connects to `pipe_name` (default: the current user's pipe), retrying until
  // `timeout_ms` expires. A failed connect leaves the client disconnected; the
  // caller falls back to pass-through.
  bool Connect(std::string* error, int timeout_ms = 2000,
               const std::wstring& pipe_name = std::wstring());
  void Close();
  bool connected() const { return pipe_ != INVALID_HANDLE_VALUE; }
  uint16_t server_protocol() const { return server_protocol_; }
  const std::string& engine_version() const { return engine_version_; }
  const std::string& resources_version() const { return resources_version_; }
  const std::string& user_data_dir() const { return user_data_dir_; }

  bool Hello(std::string* error);
  bool CreateSession(uint64_t* session, std::string* error);
  bool DestroySession(uint64_t session, std::string* error);
  bool Key(uint64_t session, int keysym, int modifiers, bool* consumed,
           std::string* error);
  bool Context(uint64_t session, RemoteSnapshot* snapshot, std::string* error);
  bool Status(uint64_t session, bool* ascii_mode, bool* disabled,
              std::string* error);
  bool SetOption(uint64_t session, const std::string& name, bool value,
                 std::string* error);
  bool Select(uint64_t session, int page_index, bool* selected,
              std::string* error);
  bool Commit(uint64_t session, std::string* text_utf8, bool* has_commit,
              std::string* error);
  bool Clear(uint64_t session, std::string* error);
  // PLAN-078 T-02 confirmation contract: report a successful host edit, or
  // compensate a failed one by removing the learned entry for (code, text).
  bool Confirm(uint64_t session, std::string* error);
  bool Forget(uint64_t session, const std::string& code,
              const std::string& text_utf8, std::string* error);
  // Asks the owner to redeploy resources for `message.user_data_dir`. Returns
  // false when the owner is unreachable (caller may deploy in-process).
  struct DeployResult {
    bool ok = false;
    bool fallback = false;
    bool disabled = false;
    std::string schema;
    std::string version;
    std::string detail;
    std::string error;
  };
  bool Deploy(const std::string& resources_dir, const std::string& user_data_dir,
              DeployResult* result, std::string* error);
  bool Ping(uint64_t* server_ms, std::string* error);
  bool Shutdown(std::string* error);

 private:
  bool Transact(ipc::Op op, uint64_t session, const std::string& payload,
                std::string* reply, std::string* error);

  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  uint16_t server_protocol_ = 0;
  std::string engine_version_;
  std::string resources_version_;
  std::string user_data_dir_;
};

}  // namespace autoime

#endif  // AUTOIME_ENGINE_CLIENT_H_
