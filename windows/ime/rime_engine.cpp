// AutoIME - narrow librime adapter implementation (PLAN-069 T-03).
#include "rime_engine.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "deployment.h"
#include "edit_model.h"
#include "diag.h"
#include "ime.h"

namespace {

std::string JoinPath(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (!b.empty() && (b[0] == '\\' || b[0] == '/')) return b;
  if (a.back() == '\\' || a.back() == '/') return a + b;
  return a + "\\" + b;
}

std::wstring Utf8ToUtf16(const std::string& utf8);

bool FileExists(const std::string& p) {
  DWORD attrs = GetFileAttributesW(Utf8ToUtf16(p).c_str());
  return attrs != INVALID_FILE_ATTRIBUTES &&
         !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::string ReadRegistryWeaselRoot() {
  char buf[MAX_PATH]{};
  DWORD size = sizeof(buf);
  LSTATUS st = RegGetValueA(HKEY_LOCAL_MACHINE,
                            "SOFTWARE\\WOW6432Node\\Rime\\Weasel",
                            "WeaselRoot", RRF_RT_REG_SZ, nullptr, buf, &size);
  if (st != ERROR_SUCCESS) {
    size = sizeof(buf);
    st = RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Rime\\Weasel",
                      "WeaselRoot", RRF_RT_REG_SZ, nullptr, buf, &size);
  }
  return st == ERROR_SUCCESS ? std::string(buf) : std::string();
}

std::wstring Utf8ToUtf16(const std::string& utf8) {
  if (utf8.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                    static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                      &out[0], n);
  return out;
}

std::string Utf16ToUtf8(const std::wstring& wide) {
  if (wide.empty()) return std::string();
  const int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                    static_cast<int>(wide.size()), nullptr, 0,
                                    nullptr, nullptr);
  std::string out(static_cast<size_t>(n), char(0));
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                      &out[0], n, nullptr, nullptr);
  return out;
}

}  // namespace

uint64_t NextSnapshotVersion() {
  static std::atomic<uint64_t> counter{0};
  return ++counter;
}

std::string RimeEngine::EnvOr(const char* env, const std::string& fallback) {
  // PLAN-072 T-03: environment values may name Unicode directories.
  wchar_t buf[32768]{};
  const DWORD n = GetEnvironmentVariableW(Utf8ToUtf16(env).c_str(), buf,
                                          static_cast<DWORD>(32768));
  if (n > 0 && n < 32768) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n),
                                        nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), char(0));
    if (len > 0)
      WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), &out[0], len,
                          nullptr, nullptr);
    return out;
  }
  return fallback;
}

bool RimeEngine::LoadDll(std::string* error) {
  // PLAN-071 T-02 engine resolution. Release mode resolves from the install
  // directory (rime.dll + data\ sit next to the IME DLL / deploy tool), so
  // the offline installer never touches the registry. Order:
  //   1. env overrides (dev/headless runs)
  //   2. <module_dir>\rime.dll + <module_dir>\data  (installed package)
  //   3. registry WeaselRoot (dev machine with an installed Weasel)
  //   4. error -> keyboard pass-through (caller decides)
  std::string weasel_root = ReadRegistryWeaselRoot();
  std::string default_dll, default_shared;
  if (!module_dir_.empty() &&
      FileExists(JoinPath(module_dir_, "rime.dll"))) {
    default_dll = JoinPath(module_dir_, "rime.dll");
    default_shared = JoinPath(module_dir_, "data");
  } else if (!weasel_root.empty()) {
    default_dll = JoinPath(weasel_root, "rime.dll");
    default_shared = JoinPath(weasel_root, "data");
  }
  dll_path_ = EnvOr("AUTOIME_RIME_DLL", default_dll);
  shared_data_dir_ = EnvOr("AUTOIME_RIME_SHARED_DATA_DIR", default_shared);
  if (!dll_path_.empty() && !FileExists(dll_path_)) {
    *error = "rime.dll not found: " + dll_path_;
    return false;
  }
  if (dll_path_.empty() || shared_data_dir_.empty()) {
    *error = "engine not found: no AUTOIME_RIME_DLL, no <module_dir>\\rime.dll, "
             "and no registry WeaselRoot";
    return false;
  }
  module_ = LoadLibraryW(Utf8ToUtf16(dll_path_).c_str());
  if (!module_) {
    char msg[128];
    snprintf(msg, sizeof(msg), "LoadLibrary failed (GetLastError=%lu)",
             GetLastError());
    *error = std::string(msg) + ": " + dll_path_;
    return false;
  }
  using GetApiFn = RimeApi* (*)(void);
  auto get_api = reinterpret_cast<GetApiFn>(GetProcAddress(module_,
                                                           "rime_get_api"));
  if (!get_api || !(api_ = get_api()) || !RIME_PROVIDED(api_, setup) ||
      !RIME_PROVIDED(api_, initialize) ||
      !RIME_PROVIDED(api_, start_maintenance) ||
      !RIME_PROVIDED(api_, join_maintenance_thread) ||
      !RIME_PROVIDED(api_, create_session) ||
      !RIME_PROVIDED(api_, destroy_session) ||
      !RIME_PROVIDED(api_, process_key) ||
      !RIME_PROVIDED(api_, get_commit) || !RIME_PROVIDED(api_, free_commit) ||
      !RIME_PROVIDED(api_, get_context) ||
      !RIME_PROVIDED(api_, free_context) ||
      !RIME_PROVIDED(api_, clear_composition) ||
      !RIME_PROVIDED(api_, select_candidate)) {
    *error = "rime_get_api unavailable or RimeApi too old";
    return false;
  }
  return true;
}

// Pins AutoIME's schema list in the private user dir. A file the user owns
// (no AutoIME marker) is left untouched.
void RimeEngine::WriteDefaultCustomYaml() const {
  const std::string custom = JoinPath(user_data_dir_, "default.custom.yaml");
  std::string existing;
  {
    FILE* f = _wfopen(Utf8ToUtf16(custom).c_str(), L"rb");
    if (f) {
      char buffer[4096];
      const size_t n = std::fread(buffer, 1, sizeof(buffer), f);
      existing.assign(buffer, n);
      std::fclose(f);
    }
  }
  if (!existing.empty() && existing.find("# AutoIME") != 0) return;
  FILE* f = _wfopen(Utf8ToUtf16(custom).c_str(), L"wb");
  if (!f) return;
  std::fprintf(f,
               "# AutoIME (PLAN-070): pinned schema list with Luna fallback\n"
               "patch:\n"
               "  schema_list:\n"
               "    - schema: %s\n",
               schema_.c_str());
  if (schema_ != "luna_pinyin_simp")
    std::fprintf(f, "    - schema: luna_pinyin_simp\n");
  std::fclose(f);
}

bool RimeEngine::InitializeAndMaintain(bool full_check) {
  if (!api_) return false;
  CreateDirectoryW(Utf8ToUtf16(user_data_dir_).c_str(), nullptr);
  WriteDefaultCustomYaml();
  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = shared_data_dir_.c_str();
  traits.user_data_dir = user_data_dir_.c_str();
  traits.distribution_name = "AutoIME";
  traits.distribution_code_name = "autoime-ime";
  traits.distribution_version = "0.1.0";
  traits.app_name = "rime.autoime";
  api_->set_notification_handler(nullptr, nullptr);
  api_->setup(&traits);
  api_->initialize(&traits);
  api_->start_maintenance(full_check ? True : False);
  if (api_->is_maintenance_mode()) api_->join_maintenance_thread();
  return true;
}

void RimeEngine::FinalizeEngine() {
  if (api_) api_->finalize();
}

bool RimeEngine::ValidateSchema(const std::string& expected,
                                std::string* actual) {
  if (!api_ || !RIME_PROVIDED(api_, get_status)) return false;
  const RimeSessionId session = api_->create_session();
  if (!session) return false;
  // Validate the named schema itself, independent of the user's previous
  // explicit selection (which is preserved for the runtime sessions).
  if (RIME_PROVIDED(api_, select_schema))
    api_->select_schema(session, expected.c_str());
  std::string id, name;
  RIME_STRUCT(RimeStatus, status);
  if (api_->get_status(session, &status)) {
    if (status.schema_id) id = status.schema_id;
    if (status.schema_name) name = status.schema_name;
    api_->free_status(&status);
  }
  // A schema whose YAML failed to parse still reports its declared id (with
  // the display name falling back to the id), and a stale compiled artifact
  // can keep working. Require both an effective display name and a functional
  // composition probe before declaring the deployed set usable.
  bool functional = false;
  const char* probe = "nihao";
  for (const char* p = probe; *p; ++p) api_->process_key(session, *p, 0);
  RIME_STRUCT(RimeContext, ctx);
  if (api_->get_context(session, &ctx)) {
    functional = ctx.menu.num_candidates > 0 && ctx.menu.candidates &&
                 ctx.menu.candidates[0].text &&
                 ctx.menu.candidates[0].text[0] != '\0';
    api_->free_context(&ctx);
  }
  api_->clear_composition(session);
  api_->destroy_session(session);
  if (actual) *actual = id;
  ImeLog("schema validation id=%s name=%s expected=%s functional=%d",
         id.c_str(), name.c_str(), expected.c_str(), functional ? 1 : 0);
  return id == expected && !name.empty() && name != id && functional;
}

void RimeEngine::CommitResources() {
  if (!resources_dir_.empty()) deployment::Commit(user_data_dir_);
}

bool RimeEngine::RollbackResources() {
  if (resources_dir_.empty()) return false;
  std::string detail;
  const bool restored = deployment::Rollback(user_data_dir_, &detail);
  deployment_detail_ += " | " + detail;
  return restored;
}

RimeEngine::~RimeEngine() {
  if (worker_.joinable()) worker_.join();
}

void RimeEngine::StartAsync() {
  if (remote_) {
    // PLAN-078 T-03: connecting to (or starting) the owner is retryable. A
    // failed attempt leaves the engine not-ready so the host keeps typing and
    // the next attempt can reconnect; the client never becomes an owner.
    std::lock_guard<std::mutex> guard(start_mutex_);
    if (worker_.joinable()) {
      if (!finished_.load()) return;  // a previous attempt is still running
      worker_.join();
    }
    started_.store(true);
    finished_.store(false);
    worker_ = std::thread([this] {
      std::string error;
      if (!EnsureReady(&error))
        ImeLog("async shared-engine connect failed: %s", error.c_str());
      finished_.store(true);
    });
    return;
  }
  bool expected = false;
  if (!started_.compare_exchange_strong(expected, true)) return;
  worker_ = std::thread([this] {
    std::string error;
    if (!EnsureReady(&error))
      ImeLog("async engine initialization failed: %s", error.c_str());
  });
}

void RimeEngine::Shutdown() {
  if (remote_) {
    // The owner is shared by every host of this user: disconnecting our client
    // must not finalize it. Idle exit or an explicit owner shutdown owns that.
    std::lock_guard<std::mutex> guard(client_mutex_);
    if (client_) {
      client_->Close();
      client_.reset();
    }
    ready_.store(false);
    finished_.store(true);
    if (worker_.joinable()) worker_.join();
    return;
  }
  if (worker_.joinable()) worker_.join();
  std::lock_guard<std::mutex> guard(init_mutex_);
  FinalizeEngine();
  ready_.store(false);
}

bool RimeEngine::EnsureReady(std::string* error) {
  if (remote_) return EnsureRemoteReady(error);
  std::lock_guard<std::mutex> guard(init_mutex_);
  if (finished_.load()) return ready_.load();
  if (!LoadDll(error)) {
    finished_.store(true);
    diag::Failure("engine", E_FAIL);
    return false;
  }
  user_data_dir_ =
      user_override_.empty() ? deployment::DefaultUserDir() : user_override_;
  CreateDirectoryW(Utf8ToUtf16(user_data_dir_).c_str(), nullptr);
  resources_dir_ = resources_override_;
  if (resources_dir_.empty() && !module_dir_.empty())
    resources_dir_ = deployment::ResolveResourcesDir(module_dir_);

  // Hold the cross-process deployment gate across Prepare + compile + commit:
  // two hosts cold-starting together must compile the dictionaries only once.
  deployment::DeployLock deploy_lock;
  bool resources_ok = false;
  if (!resources_dir_.empty()) {
    const deployment::Status st =
        deployment::Prepare(resources_dir_, user_data_dir_);
    resources_ok = st.ok;
    deployment_version_ = st.version;
    deployment_detail_ = st.detail;
    ImeLog("deploy resources=%s version=%s copied=%d unchanged=%d detail=%s",
           resources_dir_.c_str(), st.version.c_str(), st.copied ? 1 : 0,
           st.unchanged ? 1 : 0, st.detail.c_str());
    diag::Event("deploy", st.ok ? (st.unchanged ? "unchanged" : "prepared")
                                : "failed");
  } else {
    deployment_detail_ = "no resources directory (Luna fallback)";
    diag::Event("deploy", "no_resources");
  }

  // A failed/unusable resource set that ends on the Luna schema is a FALLBACK,
  // never a success: callers (manager transaction, deploy CLI) must report it
  // as a failure or a named retry state (PLAN-072 T-03).
  schema_ = resources_ok ? "autoime_pinyin" : "luna_pinyin_simp";
  if (!resources_ok) fallback_.store(true);
  const std::string env_schema = EnvOr("AUTOIME_RIME_SCHEMA", "");
  if (!env_schema.empty()) schema_ = env_schema;

  if (!InitializeAndMaintain(true)) {
    finished_.store(true);
    deployment_detail_ += " | initialize failed";
    return false;
  }
  std::string actual;
  if (ValidateSchema(schema_, &actual)) {
    if (resources_ok) CommitResources();
    ready_.store(true);
    diag::Event("engine", "ready");
  } else {
    // 1. Restore the last known-good resource set and retry once. Finalize
    // first so the compile output is no longer mapped and can be dropped.
    FinalizeEngine();
    RollbackResources();
    if (InitializeAndMaintain(true) && ValidateSchema(schema_, &actual)) {
      ready_.store(true);
      diag::Event("engine", "ready_after_rollback");
    } else {
      // 2. Last resort: Luna schema shipped with Weasel (no AutoIME data).
      schema_ = "luna_pinyin_simp";
      schema_override_ = "luna_pinyin_simp";
      fallback_.store(true);
      FinalizeEngine();
      if (InitializeAndMaintain(true) && ValidateSchema(schema_, &actual)) {
        ready_.store(true);
        diag::Event("engine", "ready_luna");
      } else {
        disabled_.store(true);
        ready_.store(false);
        FinalizeEngine();
        diag::Event("engine", "disabled");
        if (error) *error = "engine disabled: no usable schema";
      }
    }
  }
  finished_.store(true);
  return ready_.load();
}

bool RimeEngine::Redeploy(std::string* error) {
  if (remote_ || !api_) return false;
  {
    std::lock_guard<std::mutex> guard(init_mutex_);
    FinalizeEngine();
    ready_.store(false);
    finished_.store(false);
    started_.store(false);
    disabled_.store(false);
    fallback_.store(false);
  }
  return EnsureReady(error);
}

bool RimeEngine::ResolvePaths(std::string* error) {
  user_data_dir_ =
      user_override_.empty() ? deployment::DefaultUserDir() : user_override_;
  CreateDirectoryW(Utf8ToUtf16(user_data_dir_).c_str(), nullptr);
  resources_dir_ = resources_override_;
  if (resources_dir_.empty() && !module_dir_.empty())
    resources_dir_ = deployment::ResolveResourcesDir(module_dir_);
  if (resources_dir_.empty()) {
    if (error) *error = "no resources directory for the shared engine owner";
    return false;
  }
  return true;
}

bool RimeEngine::ResolveOwnerExe(std::string* path) {
  if (!engine_exe_override_.empty()) {
    *path = engine_exe_override_;
    return true;
  }
  const std::string env = EnvOr("AUTOIME_ENGINE_EXE", "");
  if (!env.empty()) {
    *path = env;
    return true;
  }
  if (!module_dir_.empty()) {
    const std::string candidate = JoinPath(module_dir_, "autoime_engine.exe");
    if (FileExists(candidate)) {
      *path = candidate;
      return true;
    }
  }
  return false;
}

bool RimeEngine::StartOwner(const std::string& exe, std::string* error) {
  std::string command = "\"" + exe + "\" --serve --resources-dir \"" +
                        resources_dir_ + "\" --user-data-dir \"" +
                        user_data_dir_ + "\" --idle-exit-ms 600000";
  if (!remote_instance_.empty())
    command += " --instance \"" + remote_instance_ + "\"";
  const std::wstring wide_command = Utf8ToUtf16(command);
  STARTUPINFOW si = {sizeof(si)};
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi = {};
  std::vector<wchar_t> mutable_command(wide_command.begin(),
                                       wide_command.end());
  mutable_command.push_back(0);
  if (!CreateProcessW(Utf8ToUtf16(exe).c_str(), mutable_command.data(), nullptr,
                      nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                      &pi)) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "CreateProcess(autoime_engine.exe) failed (GetLastError=%lu)",
             GetLastError());
    if (error) *error = buf;
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  ImeLog("shared engine owner started: %s", exe.c_str());
  diag::Event("engine", "owner_started");
  return true;
}

bool RimeEngine::RemoteFailed(const std::string& what, const std::string& error) {
  ImeLog("shared engine %s failed: %s", what.c_str(), error.c_str());
  diag::Event("engine", "remote_lost");
  ready_.store(false);
  return false;
}

// Connect to the per-user owner, starting it when nobody serves the pipe yet.
// The wait covers the first deployment (dictionary compilation happens in the
// owner, so the client never waits on a half-written data set).
bool RimeEngine::EnsureRemoteReady(std::string* error) {
  std::lock_guard<std::mutex> guard(init_mutex_);
  {
    std::lock_guard<std::mutex> client_guard(client_mutex_);
    if (client_ && client_->connected()) {
      ready_.store(true);
      return true;
    }
  }
  if (!ResolvePaths(error)) {
    finished_.store(true);
    return false;
  }
  // One owner per user data root: an alternate/isolated root gets its own
  // deterministic instance so it cannot attach to the default profile's owner.
  if (remote_instance_.empty()) {
    std::string derived;
    if (autoime::ProfileInstanceForUserDir(user_data_dir_, &derived, nullptr)) {
      remote_instance_ = derived;
    }
  }
  std::string owner_exe;
  const bool have_owner_exe = ResolveOwnerExe(&owner_exe);
  std::string connect_error;
  std::wstring pipe_name;
  if (!autoime::UserPipeName(remote_instance_, &pipe_name, &connect_error)) {
    if (error) *error = connect_error;
    finished_.store(true);
    return false;
  }
  std::lock_guard<std::mutex> client_guard(client_mutex_);
  client_.reset(new autoime::EngineClient());
  bool connected = client_->Connect(&connect_error, 200, pipe_name);
  if (!connected) {
    if (!have_owner_exe) {
      if (error)
        *error = "no running engine owner and no autoime_engine.exe: " +
                 connect_error;
      client_.reset();
      finished_.store(true);
      return false;
    }
    if (!StartOwner(owner_exe, error)) {
      client_.reset();
      finished_.store(true);
      return false;
    }
    for (int attempt = 0; attempt < 240 && !connected; ++attempt) {
      if (client_->Connect(&connect_error, 100, pipe_name)) {
        connected = true;
        break;
      }
      Sleep(100);
    }
  }
  if (!connected) {
    if (error) *error = "engine owner did not become reachable: " + connect_error;
    client_.reset();
    finished_.store(true);
    return false;
  }
  if (!client_->Hello(&connect_error)) {
    if (error) *error = connect_error;
    client_.reset();
    finished_.store(true);
    return false;
  }
  deployment_version_ = client_->resources_version();
  deployment_detail_ = "shared engine owner (" + client_->engine_version() + ")";
  schema_ = "autoime_pinyin";
  ready_.store(true);
  finished_.store(true);
  ImeLog("shared engine ready: owner=%s resources=%s user=%s",
         client_->engine_version().c_str(),
         client_->resources_version().c_str(),
         client_->user_data_dir().c_str());
  diag::Event("engine", "remote_ready");
  return true;
}

bool RimeEngine::ShutdownOwner(std::string* error) {
  if (!remote_) return false;
  std::lock_guard<std::mutex> guard(client_mutex_);
  if (!client_ || !client_->connected()) {
    if (error) *error = "shared engine owner not connected";
    return false;
  }
  const bool ok = client_->Shutdown(error);
  ready_.store(false);
  finished_.store(true);
  client_.reset();
  return ok;
}

bool RimeEngine::ConfirmCommit(uintptr_t session) {  if (!remote_) return true;  // in-process learning is already authoritative
  std::lock_guard<std::mutex> guard(client_mutex_);
  if (!client_ || !client_->connected()) return false;
  std::string error;
  if (!client_->Confirm(static_cast<uint64_t>(session), &error))
    return RemoteFailed("confirm", error);
  return true;
}

bool RimeEngine::ForgetCommit(uintptr_t session,
                              const std::string& text_utf8) {
  if (!remote_) return true;
  std::lock_guard<std::mutex> guard(client_mutex_);
  if (!client_ || !client_->connected()) return false;
  std::string error;
  if (!client_->Forget(static_cast<uint64_t>(session), "", text_utf8, &error))
    return RemoteFailed("forget", error);
  return true;
}

uintptr_t RimeEngine::CreateSession() {
  if (remote_) {
    std::lock_guard<std::mutex> guard(client_mutex_);
    if (!client_ || !client_->connected()) return 0;
    uint64_t session = 0;
    std::string error;
    if (!client_->CreateSession(&session, &error)) {
      RemoteFailed("create_session", error);
      return 0;
    }
    return static_cast<uintptr_t>(session);
  }
  if (!api_) return 0;
  const uintptr_t session = api_->create_session();
  if (session && !schema_override_.empty() &&
      RIME_PROVIDED(api_, select_schema))
    api_->select_schema(session, schema_override_.c_str());
  return session;
}

void RimeEngine::DestroySession(uintptr_t session) {
  if (remote_) {
    if (!session) return;
    std::lock_guard<std::mutex> guard(client_mutex_);
    if (!client_ || !client_->connected()) return;
    std::string error;
    client_->DestroySession(static_cast<uint64_t>(session), &error);
    return;
  }
  if (api_ && session) api_->destroy_session(session);
}

bool RimeEngine::ProcessKey(uintptr_t session, int keysym, int modifiers) {
  if (remote_) {
    if (!session) return false;
    std::lock_guard<std::mutex> guard(client_mutex_);
    if (!client_ || !client_->connected())
      return RemoteFailed("key", "not connected");
    bool consumed = false;
    std::string error;
    if (!client_->Key(static_cast<uint64_t>(session), keysym, modifiers,
                      &consumed, &error)) {
      return RemoteFailed("key", error);
    }
    return consumed;
  }
  return api_ && session && api_->process_key(session, keysym, modifiers);
}

bool RimeEngine::GetStatus(uintptr_t session, bool* ascii_mode, bool* disabled) {
  if (remote_) {
    if (!session) return false;
    std::lock_guard<std::mutex> guard(client_mutex_);
    if (!client_ || !client_->connected()) return false;
    bool ascii = false;
    bool engine_disabled = false;
    std::string error;
    if (!client_->Status(static_cast<uint64_t>(session), &ascii,
                         &engine_disabled, &error)) {
      return RemoteFailed("status", error);
    }
    if (ascii_mode) *ascii_mode = ascii;
    if (disabled) *disabled = engine_disabled;
    return true;
  }
  if (!api_ || !RIME_PROVIDED(api_, get_status)) return false;
  RIME_STRUCT(RimeStatus, status);
  if (!session || !api_->get_status(session, &status)) return false;
  if (ascii_mode) *ascii_mode = status.is_ascii_mode != 0;
  if (disabled) *disabled = status.is_disabled != 0;
  api_->free_status(&status);
  return true;
}

void RimeEngine::SetOption(uintptr_t session, const char* option, bool value) {
  if (remote_) {
    if (!session || !option) return;
    std::lock_guard<std::mutex> guard(client_mutex_);
    if (!client_ || !client_->connected()) return;
    std::string error;
    client_->SetOption(static_cast<uint64_t>(session), option, value, &error);
    return;
  }
  if (api_ && session && option && RIME_PROVIDED(api_, set_option))
    api_->set_option(session, option, value ? True : False);
}

bool RimeEngine::GetContext(uintptr_t session, Snapshot* out) {
  if (remote_) {
    if (!session) return false;
    autoime::RemoteSnapshot remote_snapshot;
    std::string error;
    {
      std::lock_guard<std::mutex> guard(client_mutex_);
      if (!client_ || !client_->connected())
        return RemoteFailed("context", "not connected");
      if (!client_->Context(static_cast<uint64_t>(session), &remote_snapshot,
                            &error)) {
        return RemoteFailed("context", error);
      }
    }
    *out = Snapshot{};
    out->preedit_utf16 = remote_snapshot.preedit_utf16;
    out->preedit_utf8 = Utf16ToUtf8(remote_snapshot.preedit_utf16);
    out->preedit_cursor = remote_snapshot.preedit_cursor;
    out->preedit_sel_start = remote_snapshot.preedit_sel_start;
    out->preedit_sel_end = remote_snapshot.preedit_sel_end;
    out->is_composing = remote_snapshot.is_composing;
    out->candidates = remote_snapshot.candidates;
    out->labels = remote_snapshot.labels;
    out->comments = remote_snapshot.comments;
    out->highlighted = remote_snapshot.highlighted;
    out->page_no = remote_snapshot.page_no;
    out->is_last_page = remote_snapshot.is_last_page;
    out->select_keys = remote_snapshot.select_keys;
    out->version = remote_snapshot.version;
    out->commit_preview_utf8 = remote_snapshot.commit_preview_utf8;
    return true;
  }
  if (!api_ || !session) return false;
  RIME_STRUCT(RimeContext, ctx);
  if (!api_->get_context(session, &ctx)) return false;
  *out = Snapshot{};
  if (ctx.composition.preedit) {
    out->preedit_utf8 = ctx.composition.preedit;
    out->preedit_utf16 = Utf8ToUtf16(out->preedit_utf8);
    int codepoints = 0;
    for (size_t i = 0; i < out->preedit_utf8.size(); ++i) {
      const unsigned char c = static_cast<unsigned char>(out->preedit_utf8[i]);
      if ((c & 0xC0) != 0x80) ++codepoints;
    }
    const int cursor = ctx.composition.cursor_pos;
    const int clamped_cursor =
        (cursor >= 0 && cursor <= codepoints) ? cursor : codepoints;
    out->preedit_cursor =
        autoime::Utf16OffsetFromUtf8Codepoint(out->preedit_utf8, clamped_cursor);
    int sel_start = ctx.composition.sel_start;
    int sel_end = ctx.composition.sel_end;
    if (sel_start < 0 || sel_end <= sel_start) {
      out->preedit_sel_start = out->preedit_cursor;
      out->preedit_sel_end = out->preedit_cursor;
    } else {
      if (sel_start > codepoints) sel_start = codepoints;
      if (sel_end > codepoints) sel_end = codepoints;
      out->preedit_sel_start =
          autoime::Utf16OffsetFromUtf8Codepoint(out->preedit_utf8, sel_start);
      out->preedit_sel_end =
          autoime::Utf16OffsetFromUtf8Codepoint(out->preedit_utf8, sel_end);
    }
  }
  out->is_composing = ctx.composition.length > 0;
  out->page_no = ctx.menu.page_no;
  out->is_last_page = ctx.menu.is_last_page != 0;
  if (ctx.menu.select_keys) out->select_keys = ctx.menu.select_keys;
  // The engine reports the whole candidate list; the snapshot keeps only the
  // current page and pairs every entry with its real selection label
  // (PLAN-070 T-03: labels, page index and page version).
  const int total = ctx.menu.num_candidates;
  const int page_size =
      ctx.menu.page_size > 0 ? ctx.menu.page_size : (total > 0 ? total : 1);
  int begin = ctx.menu.page_no * page_size;
  if (begin >= total) begin = 0;
  int end = begin + page_size;
  if (end > total) end = total;
  for (int i = begin; i < end && ctx.menu.candidates; ++i) {
    out->candidates.push_back(ctx.menu.candidates[i].text
                                  ? Utf8ToUtf16(ctx.menu.candidates[i].text)
                                  : std::wstring());
    out->comments.push_back(ctx.menu.candidates[i].comment
                                ? Utf8ToUtf16(ctx.menu.candidates[i].comment)
                                : std::wstring());
    std::wstring label;
    if (ctx.select_labels && ctx.select_labels[i] &&
        ctx.select_labels[i][0]) {
      label = Utf8ToUtf16(ctx.select_labels[i]);
    } else {
      const int rel = i - begin;
      if (rel < static_cast<int>(out->select_keys.size())) {
        label = std::wstring(
            1, static_cast<wchar_t>(
                   static_cast<unsigned char>(out->select_keys[rel])));
      } else {
        label = std::to_wstring(rel + 1);
      }
    }
    out->labels.push_back(label);
  }
  out->highlighted = ctx.menu.highlighted_candidate_index;
  if (out->highlighted >= begin && out->highlighted < end)
    out->highlighted -= begin;
  else if (out->highlighted >= page_size)
    out->highlighted -= begin;
  out->version = NextSnapshotVersion();
  if (ctx.commit_text_preview)
    out->commit_preview_utf8 = ctx.commit_text_preview;
  api_->free_context(&ctx);
  return true;
}

std::string RimeEngine::TakeCommit(uintptr_t session, bool* has_commit) {
  *has_commit = false;
  if (remote_) {
    if (!session) return "";
    std::lock_guard<std::mutex> guard(client_mutex_);
    if (!client_ || !client_->connected()) {
      RemoteFailed("commit", "not connected");
      return "";
    }
    std::string text;
    bool has = false;
    std::string error;
    if (!client_->Commit(static_cast<uint64_t>(session), &text, &has, &error)) {
      RemoteFailed("commit", error);
      return "";
    }
    *has_commit = has;
    return has ? text : std::string();
  }
  if (!api_ || !session) return "";
  RIME_STRUCT(RimeCommit, commit);
  std::string text;
  if (api_->get_commit(session, &commit)) {
    *has_commit = true;
    if (commit.text) text = commit.text;
    api_->free_commit(&commit);
  }
  return text;
}

void RimeEngine::ClearComposition(uintptr_t session) {
  if (remote_) {
    if (!session) return;
    std::lock_guard<std::mutex> guard(client_mutex_);
    if (!client_ || !client_->connected()) return;
    std::string error;
    client_->Clear(static_cast<uint64_t>(session), &error);
    return;
  }
  if (api_ && session) api_->clear_composition(session);
}

bool RimeEngine::SelectCandidateOnPage(uintptr_t session, size_t page_index) {
  if (remote_) {
    if (!session) return false;
    std::lock_guard<std::mutex> guard(client_mutex_);
    if (!client_ || !client_->connected())
      return RemoteFailed("select", "not connected");
    bool selected = false;
    std::string error;
    if (!client_->Select(static_cast<uint64_t>(session),
                         static_cast<int>(page_index), &selected, &error)) {
      return RemoteFailed("select", error);
    }
    return selected;
  }
  if (!api_ || !session) return false;
  if (RIME_PROVIDED(api_, select_candidate_on_current_page))
    return api_->select_candidate_on_current_page(session, page_index) != 0;
  return false;
}

void RimeEngine::SelectCandidate(uintptr_t session, size_t index) {
  // The wire protocol selects by PAGE index; an absolute index has no remote
  // meaning, so remote mode only supports SelectCandidateOnPage.
  if (remote_) return;
  if (api_ && session && RIME_PROVIDED(api_, select_candidate))
    api_->select_candidate(session, index);
}

bool RimeEngine::DeleteCandidateOnPage(uintptr_t session, size_t page_index) {
  // Owner-side only: clients compensate through ForgetCommit.
  if (remote_ || !api_ || !session) return false;
  if (RIME_PROVIDED(api_, delete_candidate_on_current_page))
    return api_->delete_candidate_on_current_page(session, page_index) != 0;
  return false;
}
