// AutoIME shared-engine client implementation (PLAN-078 T-02 prototype).
#include "engine_client.h"

#include <sddl.h>

#include <cstdio>
#include <cstring>

namespace autoime {
namespace {

std::string WinError(const char* what, DWORD code) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), " (GetLastError=%lu)", code);
  return std::string(what) + buf;
}

bool TokenUserSid(std::string* sid, std::string* error) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    *error = WinError("OpenProcessToken failed", GetLastError());
    return false;
  }
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<char> buffer(size);
  const bool ok =
      GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != 0;
  CloseHandle(token);
  if (!ok) {
    *error = WinError("GetTokenInformation(TokenUser) failed", GetLastError());
    return false;
  }
  const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
  LPWSTR text = nullptr;
  if (!ConvertSidToStringSidW(user->User.Sid, &text)) {
    *error = WinError("ConvertSidToStringSid failed", GetLastError());
    return false;
  }
  const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr,
                                    nullptr);
  sid->assign(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
  if (n > 1)
    WideCharToMultiByte(CP_UTF8, 0, text, -1, &(*sid)[0], n, nullptr, nullptr);
  LocalFree(text);
  return true;
}

}  // namespace

bool CurrentUserSidString(std::string* sid, std::string* error) {
  return TokenUserSid(sid, error);
}

bool UserPipeName(const std::string& instance, std::wstring* name,
                  std::string* error) {
  std::string sid;
  if (!TokenUserSid(&sid, error)) return false;
  std::wstring wide(sid.begin(), sid.end());
  *name = ipc::kPipePrefix + wide;
  if (!instance.empty()) *name += L"-" + std::wstring(instance.begin(), instance.end());
  return true;
}

bool UserMutexName(const std::string& instance, std::wstring* name,
                   std::string* error) {
  std::string sid;
  if (!TokenUserSid(&sid, error)) return false;
  std::wstring wide(sid.begin(), sid.end());
  *name = ipc::kMutexPrefix + wide;
  if (!instance.empty()) *name += L"." + std::wstring(instance.begin(), instance.end());
  return true;
}

bool CurrentUserPipeName(std::wstring* name, std::string* error) {
  return UserPipeName(std::string(), name, error);
}

bool CurrentUserMutexName(std::wstring* name, std::string* error) {
  return UserMutexName(std::string(), name, error);
}

namespace {

std::string CanonicalPath(std::string value) {
  for (char& c : value) {
    if (c == '/') c = '\\';
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  while (!value.empty() && value.back() == '\\') value.pop_back();
  return value;
}

uint64_t Fnv1a64(const std::string& text) {
  uint64_t hash = 1469598103934665603ull;
  for (unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string EnvValue(const char* name) {
  char buf[32768]{};
  const DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
  return (n > 0 && n < sizeof(buf)) ? std::string(buf) : std::string();
}

}  // namespace

bool ProfileInstanceForUserDir(const std::string& user_dir,
                               std::string* instance, std::string* error) {
  if (!instance) {
    if (error) *error = "instance output is null";
    return false;
  }
  instance->clear();
  if (user_dir.empty()) return true;
  const std::string local = EnvValue("LOCALAPPDATA");
  if (local.empty()) {
    if (error) *error = "LOCALAPPDATA is not set";
    return false;
  }
  const std::string default_root = CanonicalPath(local + "\\AutoIME\\rime");
  const std::string canonical = CanonicalPath(user_dir);
  if (canonical == default_root) return true;  // default profile
  char suffix[32];
  std::snprintf(suffix, sizeof(suffix), "profile-%08llx",
                static_cast<unsigned long long>(Fnv1a64(canonical) & 0xffffffffull));
  *instance = suffix;
  return true;
}

EngineClient::~EngineClient() { Close(); }

void EngineClient::Close() {
  if (pipe_ != INVALID_HANDLE_VALUE) {
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
  }
}

bool EngineClient::Connect(std::string* error, int timeout_ms,
                           const std::wstring& pipe_name) {
  Close();
  std::wstring name = pipe_name;
  if (name.empty() && !CurrentUserPipeName(&name, error)) return false;
  const DWORD deadline = GetTickCount() + static_cast<DWORD>(timeout_ms);
  for (;;) {
    const DWORD remaining =
        deadline > GetTickCount() ? deadline - GetTickCount() : 0;
    if (!WaitNamedPipeW(name.c_str(), remaining > 0 ? remaining : 1)) {
      if (GetTickCount() >= deadline) {
        *error = WinError("WaitNamedPipe timed out", GetLastError());
        return false;
      }
      Sleep(20);
      continue;
    }
    HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
      if (GetTickCount() >= deadline) {
        *error = WinError("CreateFile on engine pipe failed", GetLastError());
        return false;
      }
      Sleep(20);
      continue;
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
      *error = WinError("SetNamedPipeHandleState failed", GetLastError());
      CloseHandle(pipe);
      return false;
    }
    pipe_ = pipe;
    return true;
  }
}

bool EngineClient::Transact(ipc::Op op, uint64_t session,
                            const std::string& payload, std::string* reply,
                            std::string* error) {
  if (pipe_ == INVALID_HANDLE_VALUE) {
    *error = "engine not connected";
    return false;
  }
  if (payload.size() > ipc::kMaxPayload) {
    *error = "request payload too large";
    return false;
  }
  ipc::Header header{};
  header.magic = ipc::kMagic;
  header.version = ipc::kProtocolVersion;
  header.op = static_cast<uint16_t>(op);
  header.payload_len = static_cast<uint32_t>(payload.size());
  header.session = session;
  std::string message(reinterpret_cast<const char*>(&header), sizeof(header));
  message += payload;
  DWORD written = 0;
  if (!WriteFile(pipe_, message.data(), static_cast<DWORD>(message.size()),
                 &written, nullptr) ||
      written != message.size()) {
    *error = WinError("engine pipe write failed", GetLastError());
    Close();
    return false;
  }
  std::vector<char> buffer(ipc::kMaxPayload);
  DWORD read = 0;
  if (!ReadFile(pipe_, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                nullptr)) {
    *error = WinError("engine pipe read failed", GetLastError());
    Close();
    return false;
  }
  if (read < sizeof(ipc::Header)) {
    *error = "engine reply too short";
    Close();
    return false;
  }
  ipc::Header reply_header{};
  std::memcpy(&reply_header, buffer.data(), sizeof(reply_header));
  if (reply_header.magic != ipc::kMagic ||
      reply_header.version != ipc::kProtocolVersion) {
    *error = "engine reply has an incompatible protocol";
    Close();
    return false;
  }
  if (read != sizeof(ipc::Header) + reply_header.payload_len) {
    *error = "engine reply length mismatch";
    Close();
    return false;
  }
  reply->assign(buffer.data() + sizeof(ipc::Header), reply_header.payload_len);
  return true;
}

bool EngineClient::Hello(std::string* error) {
  ipc::Writer out;
  out.U32(GetCurrentProcessId());
  std::string reply;
  if (!Transact(ipc::Op::kHello, 0, out.data(), &reply, error)) return false;
  ipc::Reader in(reply.data(), reply.size());
  int32_t ok = 0;
  std::string version, resources, user_dir;
  if (!in.I32(&ok) || !in.Str(&version) || !in.Str(&resources) ||
      !in.Str(&user_dir)) {
    *error = "malformed hello reply";
    return false;
  }
  if (ok == 0) {
    *error = "engine rejected the client protocol version";
    Close();
    return false;
  }
  engine_version_ = version;
  resources_version_ = resources;
  user_data_dir_ = user_dir;
  server_protocol_ = ipc::kProtocolVersion;
  return true;
}

bool EngineClient::CreateSession(uint64_t* session, std::string* error) {
  std::string reply;
  if (!Transact(ipc::Op::kCreateSession, 0, std::string(), &reply, error))
    return false;
  ipc::Reader in(reply.data(), reply.size());
  int32_t ok = 0;
  uint64_t created = 0;
  if (!in.I32(&ok) || !in.U64(&created)) {
    *error = "malformed create-session reply";
    return false;
  }
  if (ok == 0 || created == 0) {
    *error = "engine could not create a session";
    return false;
  }
  *session = created;
  return true;
}

bool EngineClient::DestroySession(uint64_t session, std::string* error) {
  std::string reply;
  return Transact(ipc::Op::kDestroySession, session, std::string(), &reply,
                  error);
}

bool EngineClient::Key(uint64_t session, int keysym, int modifiers,
                       bool* consumed, std::string* error) {
  ipc::Writer out;
  out.I32(keysym);
  out.I32(modifiers);
  std::string reply;
  if (!Transact(ipc::Op::kKey, session, out.data(), &reply, error)) return false;
  ipc::Reader in(reply.data(), reply.size());
  int32_t value = 0;
  uint8_t session_ok = 0;
  if (!in.I32(&value) || !in.U8(&session_ok)) {
    *error = "malformed key reply";
    return false;
  }
  if (session_ok == 0) {
    *error = "engine session no longer exists";
    return false;
  }
  *consumed = value != 0;
  return true;
}

bool EngineClient::Context(uint64_t session, RemoteSnapshot* snapshot,
                           std::string* error) {
  std::string reply;
  if (!Transact(ipc::Op::kContext, session, std::string(), &reply, error))
    return false;
  ipc::Reader in(reply.data(), reply.size());
  uint8_t flags = 0;
  uint32_t count = 0;
  if (!in.U64(&snapshot->version) || !in.U8(&flags) ||
      !in.WStr(&snapshot->preedit_utf16) || !in.I32(&snapshot->preedit_cursor) ||
      !in.I32(&snapshot->preedit_sel_start) ||
      !in.I32(&snapshot->preedit_sel_end) || !in.I32(&snapshot->highlighted) ||
      !in.I32(&snapshot->page_no) || !in.Str(&snapshot->select_keys) ||
      !in.Str(&snapshot->commit_preview_utf8) || !in.U32(&count)) {
    *error = "malformed context reply";
    return false;
  }
  snapshot->is_composing = (flags & ipc::kSnapComposing) != 0;
  snapshot->is_last_page = (flags & ipc::kSnapLastPage) != 0;
  snapshot->session_ok = (flags & ipc::kSnapSessionOk) != 0;
  if (!snapshot->session_ok) {
    *error = "engine session no longer exists";
    return false;
  }
  snapshot->candidates.clear();
  snapshot->labels.clear();
  snapshot->comments.clear();
  for (uint32_t i = 0; i < count; ++i) {
    std::wstring candidate, label, comment;
    if (!in.WStr(&candidate) || !in.WStr(&label) || !in.WStr(&comment)) {
      *error = "malformed candidate list in reply";
      return false;
    }
    snapshot->candidates.push_back(std::move(candidate));
    snapshot->labels.push_back(std::move(label));
    snapshot->comments.push_back(std::move(comment));
  }
  if (count == 0) {
    // Keep the pre-edit text consistent even when the page is empty.
    snapshot->highlighted = 0;
  }
  return true;
}

bool EngineClient::Status(uint64_t session, bool* ascii_mode, bool* disabled,
                          std::string* error) {
  std::string reply;
  if (!Transact(ipc::Op::kStatus, session, std::string(), &reply, error))
    return false;
  ipc::Reader in(reply.data(), reply.size());
  int32_t ascii = 0;
  int32_t disabled_flag = 0;
  if (!in.I32(&ascii) || !in.I32(&disabled_flag)) {
    *error = "malformed status reply";
    return false;
  }
  *ascii_mode = ascii != 0;
  *disabled = disabled_flag != 0;
  return true;
}

bool EngineClient::SetOption(uint64_t session, const std::string& name,
                             bool value, std::string* error) {
  ipc::Writer out;
  out.Str(name);
  out.I32(value ? 1 : 0);
  std::string reply;
  return Transact(ipc::Op::kSetOption, session, out.data(), &reply, error);
}

bool EngineClient::Select(uint64_t session, int page_index, bool* selected,
                          std::string* error) {
  ipc::Writer out;
  out.I32(page_index);
  std::string reply;
  if (!Transact(ipc::Op::kSelect, session, out.data(), &reply, error))
    return false;
  ipc::Reader in(reply.data(), reply.size());
  int32_t value = 0;
  if (!in.I32(&value)) {
    *error = "malformed select reply";
    return false;
  }
  *selected = value != 0;
  return true;
}

bool EngineClient::Commit(uint64_t session, std::string* text_utf8,
                          bool* has_commit, std::string* error) {
  std::string reply;
  if (!Transact(ipc::Op::kCommit, session, std::string(), &reply, error))
    return false;
  ipc::Reader in(reply.data(), reply.size());
  int32_t has = 0;
  std::string text;
  if (!in.I32(&has) || !in.Str(&text)) {
    *error = "malformed commit reply";
    return false;
  }
  *has_commit = has != 0;
  *text_utf8 = std::move(text);
  return true;
}

bool EngineClient::Clear(uint64_t session, std::string* error) {
  std::string reply;
  return Transact(ipc::Op::kClear, session, std::string(), &reply, error);
}

bool EngineClient::Confirm(uint64_t session, std::string* error) {
  std::string reply;
  if (!Transact(ipc::Op::kConfirm, session, std::string(), &reply, error))
    return false;
  ipc::Reader in(reply.data(), reply.size());
  int32_t ok = 0;
  if (!in.I32(&ok)) {
    *error = "malformed confirm reply";
    return false;
  }
  return ok != 0;
}

bool EngineClient::Forget(uint64_t session, const std::string& code,
                          const std::string& text_utf8, std::string* error) {
  ipc::Writer out;
  out.Str(code);
  out.Str(text_utf8);
  std::string reply;
  if (!Transact(ipc::Op::kForget, session, out.data(), &reply, error))
    return false;
  ipc::Reader in(reply.data(), reply.size());
  int32_t ok = 0;
  if (!in.I32(&ok)) {
    *error = "malformed forget reply";
    return false;
  }
  return ok != 0;
}

bool EngineClient::Ping(uint64_t* server_ms, std::string* error) {
  std::string reply;
  if (!Transact(ipc::Op::kPing, 0, std::string(), &reply, error)) return false;
  ipc::Reader in(reply.data(), reply.size());
  uint64_t tick = 0;
  if (!in.U64(&tick)) {
    *error = "malformed ping reply";
    return false;
  }
  *server_ms = tick;
  return true;
}

bool EngineClient::Shutdown(std::string* error) {
  std::string reply;
  const bool ok = Transact(ipc::Op::kShutdown, 0, std::string(), &reply, error);
  Close();
  return ok;
}

bool EngineClient::Deploy(const std::string& resources_dir,
                          const std::string& user_data_dir,
                          DeployResult* result, std::string* error) {
  ipc::Writer out;
  out.Str(resources_dir);
  out.Str(user_data_dir);
  std::string reply;
  if (!Transact(ipc::Op::kDeploy, 0, out.data(), &reply, error)) return false;
  ipc::Reader in(reply.data(), reply.size());
  int32_t ok = 0, fallback = 0, disabled = 0;
  if (!in.I32(&ok) || !in.I32(&fallback) || !in.I32(&disabled) ||
      !in.Str(&result->schema) || !in.Str(&result->version) ||
      !in.Str(&result->detail) || !in.Str(&result->error)) {
    *error = "malformed deploy reply";
    return false;
  }
  result->ok = ok != 0;
  result->fallback = fallback != 0;
  result->disabled = disabled != 0;
  return true;
}

}  // namespace autoime
