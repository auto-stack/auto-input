// AutoIME shared engine owner (PLAN-078 T-02 prototype, productized by T-03).
//
// One process per interactive user owns librime and the writable user
// dictionary. Clients (TSF DLL, manager, deploy tool) connect to the per-user
// named pipe and forward session operations. Because every session lives in
// this single process, a confirmation learned in one host is immediately
// visible to the next composition in another host, and only this process opens
// %LOCALAPPDATA%\AutoIME\rime\autoime_user.userdb (no LOCK contention).
//
// Startup order: user mutex (single owner) -> RimeEngine deploy/initialize ->
// accept loop. A second instance exits with code 8 so callers use the running
// owner. Clients are authenticated by their token user SID; a mismatching
// client is disconnected before it can touch a session.
#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "engine_client.h"
#include "protocol.h"
#include "../ime/diag.h"
#include "../ime/rime_engine.h"

// The protocol lives in autoime::ipc; the server code reads better with the
// short alias.
namespace ipc = autoime::ipc;

void ImeLog(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::fprintf(stderr, "[engine] ");
  std::vfprintf(stderr, fmt, args);
  std::fprintf(stderr, "\n");
  va_end(args);
}

namespace {

constexpr const char* kEngineVersion = "autoime-engine 0.3.0/config1";

struct Options {
  std::wstring pipe_name;
  std::string resources_dir;
  std::string user_data_dir;
  std::string instance;  // side-by-side owner (tests, staged upgrades)
  int idle_exit_ms = 0;  // 0 = stay until shutdown/kill
  bool serve = false;
  bool help = false;
};

RimeEngine g_engine;
std::mutex g_engine_mutex;
std::atomic<int> g_clients{0};
std::atomic<bool> g_stop{false};
std::atomic<uint64_t> g_last_activity_ms{0};

uint64_t NowMs() { return GetTickCount64(); }

std::wstring Utf8ToWide(const std::string& utf8);

// Case-insensitive path identity for "is this the user dir I own?".
bool SamePath(const std::string& a, const std::string& b) {
  std::string left = a;
  std::string right = b;
  for (char& c : left) {
    if (c == '/') c = '\\';
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  for (char& c : right) {
    if (c == '/') c = '\\';
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  while (!left.empty() && left.back() == '\\') left.pop_back();
  while (!right.empty() && right.back() == '\\') right.pop_back();
  return left == right;
}

void Touch() { g_last_activity_ms.store(NowMs()); }

bool ReadMessage(HANDLE pipe, std::vector<char>* buffer, ipc::Header* header) {
  buffer->resize(ipc::kMaxPayload);
  DWORD read = 0;
  if (!ReadFile(pipe, buffer->data(), static_cast<DWORD>(buffer->size()), &read,
                nullptr)) {
    return false;
  }
  if (read < sizeof(ipc::Header)) return false;
  std::memcpy(header, buffer->data(), sizeof(*header));
  if (header->magic != ipc::kMagic || header->version != ipc::kProtocolVersion)
    return false;
  if (read != sizeof(ipc::Header) + header->payload_len) return false;
  Touch();
  return true;
}

bool WriteMessage(HANDLE pipe, ipc::Op op, uint64_t session,
                  const std::string& payload) {
  ipc::Header header{};
  header.magic = ipc::kMagic;
  header.version = ipc::kProtocolVersion;
  header.op = static_cast<uint16_t>(op);
  header.payload_len = static_cast<uint32_t>(payload.size());
  header.session = session;
  std::string message(reinterpret_cast<const char*>(&header), sizeof(header));
  message += payload;
  DWORD written = 0;
  return WriteFile(pipe, message.data(), static_cast<DWORD>(message.size()),
                   &written, nullptr) &&
         written == message.size();
}

bool ClientIsOwner(HANDLE pipe) {
  ULONG pid = 0;
  if (!GetNamedPipeClientProcessId(pipe, &pid)) return false;
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return false;
  HANDLE token = nullptr;
  const bool opened = OpenProcessToken(process, TOKEN_QUERY, &token) != 0;
  CloseHandle(process);
  if (!opened) return false;
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<char> buffer(size);
  const bool ok =
      GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != 0;
  CloseHandle(token);
  if (!ok) return false;
  const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
  LPWSTR client_sid = nullptr;
  if (!ConvertSidToStringSidW(user->User.Sid, &client_sid)) return false;
  std::string sid;
  if (!autoime::CurrentUserSidString(&sid, nullptr)) {
    LocalFree(client_sid);
    return false;
  }
  std::wstring wide(sid.begin(), sid.end());
  const bool same = wide == client_sid;
  LocalFree(client_sid);
  return same;
}

void HandleConnection(HANDLE pipe) {
  g_clients.fetch_add(1);
  std::set<uint64_t> sessions;
  // PLAN-078 review F-01: the TSF side forgets a commit without repeating the
  // code, so remember the last confirmed (code, text) per session.
  struct Selected { std::string code; std::string text; };
  std::map<uint64_t, Selected> last_selected;
  std::map<uint64_t, std::string> last_code;
  bool hello_ok = false;
  std::vector<char> buffer;
  ipc::Header header{};
  while (!g_stop.load() && ReadMessage(pipe, &buffer, &header)) {
    const char* payload = buffer.data() + sizeof(ipc::Header);
    const size_t payload_size = header.payload_len;
    ipc::Writer out;
    // PLAN-078 review F-08: every connection must authenticate (kHello) before
    // it can touch sessions; kPing stays open for liveness probes.
    const ipc::Op op = static_cast<ipc::Op>(header.op);
    if (!hello_ok && op != ipc::Op::kHello && op != ipc::Op::kPing) {
      out.I32(0);
      WriteMessage(pipe, op, header.session, out.data());
      continue;
    }
    switch (op) {
      case ipc::Op::kHello: {
        if (!ClientIsOwner(pipe)) {
          out.I32(0);
          out.Str("client user does not match the engine owner");
          out.Str("");
          WriteMessage(pipe, ipc::Op::kHelloReply, 0, out.data());
          CloseHandle(pipe);
          g_clients.fetch_sub(1);
          return;
        }
        hello_ok = true;
        out.I32(1);
        out.Str(kEngineVersion);
        out.Str(g_engine.deployment_version());
        out.Str(g_engine.user_data_dir());
        WriteMessage(pipe, ipc::Op::kHelloReply, 0, out.data());
        break;
      }
      case ipc::Op::kCreateSession: {
        std::lock_guard<std::mutex> lock(g_engine_mutex);
        const uintptr_t session = g_engine.CreateSession();
        sessions.insert(static_cast<uint64_t>(session));
        out.I32(session != 0 ? 1 : 0);
        out.U64(static_cast<uint64_t>(session));
        WriteMessage(pipe, ipc::Op::kCreateSessionReply, 0, out.data());
        break;
      }
      case ipc::Op::kDestroySession: {
        last_selected.erase(header.session);
        last_code.erase(header.session);
        if (sessions.erase(header.session) != 0) {
          std::lock_guard<std::mutex> lock(g_engine_mutex);
          g_engine.DestroySession(static_cast<uintptr_t>(header.session));
        }
        out.I32(1);
        WriteMessage(pipe, ipc::Op::kDestroySession, header.session,
                     out.data());
        break;
      }
      case ipc::Op::kKey: {
        ipc::Reader in(payload, payload_size);
        int32_t keysym = 0;
        int32_t modifiers = 0;
        if (!in.I32(&keysym) || !in.I32(&modifiers) ||
            sessions.count(header.session) == 0) {
          out.I32(0);
          WriteMessage(pipe, ipc::Op::kKey, header.session, out.data());
          break;
        }
        bool consumed = false;
        const bool known = sessions.count(header.session) != 0;
        if (known) {
          std::lock_guard<std::mutex> lock(g_engine_mutex);
          consumed = g_engine.ProcessKey(static_cast<uintptr_t>(header.session),
                                        keysym, modifiers);
        }
        out.I32(consumed ? 1 : 0);
        out.U8(known ? 1 : 0);
        WriteMessage(pipe, ipc::Op::kKey, header.session, out.data());
        break;
      }
      case ipc::Op::kContext: {
        RimeEngine::Snapshot snapshot;
        const bool known = sessions.count(header.session) != 0;
        bool ok = false;
        if (known) {
          std::lock_guard<std::mutex> lock(g_engine_mutex);
          ok = g_engine.GetContext(static_cast<uintptr_t>(header.session),
                                   &snapshot);
          if (ok) {
            // PLAN-078 review N-05: a key-driven commit (Space/Return) has no
            // select, so remember the composition code on every context read.
            std::string code = snapshot.preedit_utf8;
            code.erase(std::remove_if(code.begin(), code.end(),
                                      [](unsigned char c) {
                                        return c == ' ' || c == '\t';
                                      }),
                       code.end());
            if (!code.empty()) last_code[header.session] = code;
          }
        }
        if (!ok) {
          out.U64(0);
          out.U8(known ? ipc::kSnapSessionOk : 0);
          out.WStr(std::wstring());
          out.I32(0);
          out.I32(0);
          out.I32(0);
          out.I32(0);
          out.I32(0);
          out.Str("");
          out.Str("");
          out.U32(0);
          WriteMessage(pipe, ipc::Op::kContext, header.session, out.data());
          break;
        }
        uint8_t flags = ipc::kSnapSessionOk;
        if (snapshot.is_composing) flags |= ipc::kSnapComposing;
        if (snapshot.is_last_page) flags |= ipc::kSnapLastPage;
        out.U64(snapshot.version);
        out.U8(flags);
        out.WStr(snapshot.preedit_utf16);
        out.I32(snapshot.preedit_cursor);
        out.I32(snapshot.preedit_sel_start);
        out.I32(snapshot.preedit_sel_end);
        out.I32(snapshot.highlighted);
        out.I32(snapshot.page_no);
        out.Str(snapshot.select_keys);
        out.Str(snapshot.commit_preview_utf8);
        out.U32(static_cast<uint32_t>(snapshot.candidates.size()));
        for (size_t i = 0; i < snapshot.candidates.size(); ++i) {
          out.WStr(snapshot.candidates[i]);
          out.WStr(i < snapshot.labels.size() ? snapshot.labels[i]
                                              : std::wstring());
          out.WStr(i < snapshot.comments.size() ? snapshot.comments[i]
                                                : std::wstring());
        }
        WriteMessage(pipe, ipc::Op::kContext, header.session, out.data());
        break;
      }
      case ipc::Op::kStatus: {
        bool ascii_mode = false;
        bool disabled = false;
        if (sessions.count(header.session) != 0) {
          std::lock_guard<std::mutex> lock(g_engine_mutex);
          g_engine.GetStatus(static_cast<uintptr_t>(header.session), &ascii_mode,
                            &disabled);
        }
        out.I32(ascii_mode ? 1 : 0);
        out.I32(disabled ? 1 : 0);
        WriteMessage(pipe, ipc::Op::kStatus, header.session, out.data());
        break;
      }
      case ipc::Op::kSetOption: {
        ipc::Reader in(payload, payload_size);
        std::string name;
        int32_t value = 0;
        if (in.Str(&name) && in.I32(&value) &&
            sessions.count(header.session) != 0) {
          std::lock_guard<std::mutex> lock(g_engine_mutex);
          g_engine.SetOption(static_cast<uintptr_t>(header.session),
                             name.c_str(), value != 0);
        }
        out.I32(1);
        WriteMessage(pipe, ipc::Op::kSetOption, header.session, out.data());
        break;
      }
      case ipc::Op::kSelect: {
        ipc::Reader in(payload, payload_size);
        int32_t index = -1;
        bool selected = false;
        if (in.I32(&index) && index >= 0 &&
            sessions.count(header.session) != 0) {
          std::lock_guard<std::mutex> lock(g_engine_mutex);
          // Remember the code that produced the page so a later Forget can
          // replay it without the client having to resend it.
          RimeEngine::Snapshot before;
          if (g_engine.GetContext(static_cast<uintptr_t>(header.session),
                                  &before)) {
            // librime's preedit separates syllables with spaces ("li xian");
            // the replay types raw keys, so remember the code without them.
            std::string code = before.preedit_utf8;
            code.erase(std::remove_if(code.begin(), code.end(),
                                      [](unsigned char c) {
                                        return c == ' ' || c == '\t';
                                      }),
                       code.end());
            last_code[header.session] = code;
          }
          selected = g_engine.SelectCandidateOnPage(
              static_cast<uintptr_t>(header.session),
              static_cast<size_t>(index));
        }
        out.I32(selected ? 1 : 0);
        WriteMessage(pipe, ipc::Op::kSelect, header.session, out.data());
        break;
      }
      case ipc::Op::kCommit: {
        std::string text;
        bool has = false;
        if (sessions.count(header.session) != 0) {
          std::lock_guard<std::mutex> lock(g_engine_mutex);
          text = g_engine.TakeCommit(static_cast<uintptr_t>(header.session),
                                     &has);
        }
        if (has) {
          const auto code = last_code.find(header.session);
          if (code != last_code.end()) {
            last_selected[header.session] = Selected{code->second, text};
            last_code.erase(code);
          }
        }
        out.I32(has ? 1 : 0);
        out.Str(text);
        WriteMessage(pipe, ipc::Op::kCommit, header.session, out.data());
        break;
      }
      case ipc::Op::kClear: {
        last_code.erase(header.session);
        if (sessions.count(header.session) != 0) {
          std::lock_guard<std::mutex> lock(g_engine_mutex);
          g_engine.ClearComposition(static_cast<uintptr_t>(header.session));
        }
        out.I32(1);
        WriteMessage(pipe, ipc::Op::kClear, header.session, out.data());
        break;
      }
      case ipc::Op::kConfirm: {
        // The host applied the committed text: nothing to undo.
        out.I32(sessions.count(header.session) != 0 ? 1 : 0);
        WriteMessage(pipe, ipc::Op::kConfirm, header.session, out.data());
        break;
      }
      case ipc::Op::kForget: {
        // The host failed to apply an already committed text. librime learned
        // the confirmation at select time, so replay the same code in a
        // scratch session and delete that user-dictionary entry.
        ipc::Reader in(payload, payload_size);
        std::string code, text;
        bool ok = in.Str(&code) && in.Str(&text);
        if (ok && sessions.count(header.session) == 0) ok = false;
        if (ok && code.empty()) {
          const auto remembered = last_selected.find(header.session);
          if (remembered != last_selected.end()) {
            code = remembered->second.code;
            if (text.empty()) text = remembered->second.text;
          }
        }
        // A forget that cannot find the entry is benign: the contract is
        // "do not count this learning", and librime may never have written it.
        bool deleted = false;
        if (ok && !code.empty()) {
          std::lock_guard<std::mutex> lock(g_engine_mutex);
          const uintptr_t scratch = g_engine.CreateSession();
          if (scratch != 0) {
            for (char c : code) {
              const bool upper = c >= 'A' && c <= 'Z';
              g_engine.ProcessKey(scratch, static_cast<int>(c), upper ? 1 : 0);
            }
            RimeEngine::Snapshot snapshot;
            if (g_engine.GetContext(scratch, &snapshot)) {
              const std::wstring target = Utf8ToWide(text);
              // PLAN-078 review N-05: the confirmed entry may sit on a later
              // page after other learning; walk the pages before giving up.
              for (int page = 0; page < 20; ++page) {
                for (size_t i = 0; i < snapshot.candidates.size(); ++i) {
                  if (snapshot.candidates[i] == target &&
                      g_engine.DeleteCandidateOnPage(scratch, i)) {
                    deleted = true;
                    break;
                  }
                }
                if (deleted || snapshot.is_last_page) break;
                g_engine.ProcessKey(scratch, 0xff56, 0);  // Page_Down
                if (!g_engine.GetContext(scratch, &snapshot)) break;
              }
            }
            g_engine.DestroySession(scratch);
          }
        }
        if (deleted) last_selected.erase(header.session);
        out.I32(ok ? 1 : 0);
        WriteMessage(pipe, ipc::Op::kForget, header.session, out.data());
        break;
      }
      case ipc::Op::kPing: {
        out.U64(NowMs());
        WriteMessage(pipe, ipc::Op::kPing, 0, out.data());
        break;
      }
      case ipc::Op::kDeploy: {
        // A redeploy finalizes librime, which drops every live session; clients
        // recover by recreating their session on the next composition.
        ipc::Reader in(payload, payload_size);
        std::string resources, user_dir;
        bool ok = in.Str(&resources) && in.Str(&user_dir);
        std::string deploy_error;
        if (ok && !user_dir.empty() && !SamePath(g_engine.user_data_dir(), user_dir)) {
          deploy_error = "owner serves a different user data dir";
          ok = false;
        }
        if (ok) {
          std::lock_guard<std::mutex> lock(g_engine_mutex);
          if (!resources.empty()) g_engine.SetPaths(resources, g_engine.user_data_dir());
          ok = g_engine.Redeploy(&deploy_error);
        }
        // PLAN-078 review N-04: copy the deploy result under the engine lock;
        // another connection may be inside Redeploy while this reply is built.
        bool fallback = false;
        bool disabled = false;
        std::string schema;
        std::string version;
        std::string detail;
        {
          std::lock_guard<std::mutex> lock(g_engine_mutex);
          fallback = g_engine.fallback();
          disabled = g_engine.disabled();
          schema = g_engine.schema();
          version = g_engine.deployment_version();
          detail = g_engine.deployment_detail();
        }
        out.I32(ok ? 1 : 0);
        out.I32(fallback ? 1 : 0);
        out.I32(disabled ? 1 : 0);
        out.Str(schema);
        out.Str(version);
        out.Str(detail);
        out.Str(deploy_error);
        WriteMessage(pipe, ipc::Op::kDeploy, 0, out.data());
        break;
      }
      case ipc::Op::kShutdown: {
        out.I32(1);
        WriteMessage(pipe, ipc::Op::kShutdown, 0, out.data());
        // Finalize librime (which flushes the user dictionary) before the
        // process leaves, then exit without waiting for the accept loop.
        g_stop.store(true);
        {
          std::lock_guard<std::mutex> lock(g_engine_mutex);
          g_engine.Shutdown();
        }
        std::fflush(stdout);
        std::fflush(stderr);
        ExitProcess(0);
      }
      case ipc::Op::kBye: {
        CloseHandle(pipe);
        g_clients.fetch_sub(1);
        return;
      }
      default:
        break;
    }
  }
  CloseHandle(pipe);
  g_clients.fetch_sub(1);
}

std::wstring Utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                    static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                      &out[0], n);
  return out;
}

bool ParseArgs(int argc, char** argv, Options* options, std::string* error) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&](std::string* out) {
      if (i + 1 >= argc) {
        *error = arg + " needs a value";
        return false;
      }
      *out = argv[++i];
      return true;
    };
    std::string text;
    if (arg == "--serve") {
      options->serve = true;
    } else if (arg == "--help" || arg == "-h") {
      options->help = true;
    } else if (arg == "--pipe") {
      if (!value(&text)) return false;
      options->pipe_name = Utf8ToWide(text);
    } else if (arg == "--resources-dir") {
      if (!value(&options->resources_dir)) return false;
    } else if (arg == "--user-data-dir") {
      if (!value(&options->user_data_dir)) return false;
    } else if (arg == "--instance") {
      if (!value(&options->instance)) return false;
    } else if (arg == "--idle-exit-ms") {
      if (!value(&text)) return false;
      options->idle_exit_ms = std::atoi(text.c_str());
    } else {
      *error = "unknown argument: " + arg;
      return false;
    }
  }
  return true;
}

// Prints one machine-readable line the harness and the installer can gate on.
void PrintStatus(bool ready, long long deploy_ms, const std::string& error) {
  std::printf("{\"engine\":\"%s\",\"ready\":%s,\"deploy_ms\":%lld,"
              "\"schema\":\"%s\",\"resources_version\":\"%s\","
              "\"resources_dir\":\"%s\",\"user_data_dir\":\"%s\","
              "\"dll\":\"%s\",\"error\":\"%s\"}\n",
              kEngineVersion, ready ? "true" : "false", deploy_ms,
              g_engine.schema().c_str(), g_engine.deployment_version().c_str(),
              g_engine.resources_dir().c_str(), g_engine.user_data_dir().c_str(),
              g_engine.dll_path().c_str(), error.c_str());
  std::fflush(stdout);
}

// PLAN-078 T-02: an owner that nobody talks to must not keep the user
// dictionary open forever (upgrades, logoff, debugging). The monitor finalizes
// librime before exiting so the last learnings are flushed.
void IdleMonitor(int idle_exit_ms) {
  while (!g_stop.load()) {
    Sleep(500);
    if (g_clients.load() > 0) continue;
    const uint64_t last = g_last_activity_ms.load();
    if (last != 0 && NowMs() - last < static_cast<uint64_t>(idle_exit_ms))
      continue;
    {
      std::lock_guard<std::mutex> lock(g_engine_mutex);
      g_engine.Shutdown();
    }
    std::fflush(stdout);
    std::fflush(stderr);
    ExitProcess(0);
  }
}

}  // namespace

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);
  Options options;
  std::string error;
  if (!ParseArgs(argc, argv, &options, &error)) {
    std::fprintf(stderr, "ERROR bad usage: %s\n", error.c_str());
    return 2;
  }
  if (options.help || !options.serve) {
    std::printf(
        "usage: autoime_engine.exe --serve [--pipe NAME] [--instance NAME]\n"
        "       --resources-dir DIR --user-data-dir DIR [--idle-exit-ms N]\n");
    return options.help ? 0 : 2;
  }
  if (options.user_data_dir.empty()) {
    std::fprintf(stderr, "ERROR --user-data-dir is required\n");
    return 2;
  }
  std::wstring pipe_name = options.pipe_name;
  if (pipe_name.empty() &&
      !autoime::UserPipeName(options.instance, &pipe_name, &error)) {
    std::fprintf(stderr, "ERROR %s\n", error.c_str());
    return 2;
  }
  std::wstring mutex_name;
  if (!autoime::UserMutexName(options.instance, &mutex_name, &error)) {
    std::fprintf(stderr, "ERROR %s\n", error.c_str());
    return 2;
  }
  HANDLE mutex = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
  if (!mutex) {
    std::fprintf(stderr, "ERROR CreateMutex failed (GetLastError=%lu)\n",
                 GetLastError());
    return 2;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    std::printf("{\"engine\":\"%s\",\"ready\":false,\"already_running\":true}\n",
                kEngineVersion);
    std::fflush(stdout);
    return 8;  // another owner is already serving this user
  }

  g_engine.SetPaths(options.resources_dir, options.user_data_dir);
  const long long deploy_start = static_cast<long long>(NowMs());
  if (!g_engine.EnsureReady(&error)) {
    PrintStatus(false, static_cast<long long>(NowMs()) - deploy_start, error);
    return 4;
  }
  const long long deploy_ms = static_cast<long long>(NowMs()) - deploy_start;
  PrintStatus(true, deploy_ms, "");
  Touch();

  // PLAN-078 follow-up: an owner started by the elevated installer must still be
  // usable by the normal-privilege IME. The default DACL depends on the token
  // that created the process, so grant the current user SID explicitly.
  std::string user_sid;
  std::string sid_error;
  PSECURITY_DESCRIPTOR pipe_sd = nullptr;
  SECURITY_ATTRIBUTES pipe_sa{};
  if (autoime::CurrentUserSidString(&user_sid, &sid_error) && !user_sid.empty()) {
    const std::wstring sddl =
        L"D:(A;;GA;;;" + std::wstring(user_sid.begin(), user_sid.end()) + L")";
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &pipe_sd, nullptr)) {
      pipe_sa.nLength = sizeof(pipe_sa);
      pipe_sa.lpSecurityDescriptor = pipe_sd;
      pipe_sa.bInheritHandle = FALSE;
      std::fprintf(stderr, "[engine] pipe dacl user=%s\n", user_sid.c_str());
    }
  }
  HANDLE pipe = INVALID_HANDLE_VALUE;
  bool first = true;
  if (options.idle_exit_ms > 0) {
    std::thread(IdleMonitor, options.idle_exit_ms).detach();
  }
  while (!g_stop.load()) {
    if (pipe == INVALID_HANDLE_VALUE) {
      pipe = CreateNamedPipeW(
          pipe_name.c_str(), PIPE_ACCESS_DUPLEX,
          PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
              PIPE_REJECT_REMOTE_CLIENTS,
          16, static_cast<DWORD>(ipc::kMaxPayload),
          static_cast<DWORD>(ipc::kMaxPayload), 0,
          pipe_sd ? &pipe_sa : nullptr);
      if (pipe == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "ERROR CreateNamedPipe failed (GetLastError=%lu)\n",
                     GetLastError());
        return 2;
      }
      if (first) {
        std::printf("{\"engine\":\"%s\",\"listening\":true,\"pipe\":\"%ls\"}\n",
                    kEngineVersion, pipe_name.c_str());
        std::fflush(stdout);
        first = false;
      }
    }
    const BOOL connected = ConnectNamedPipe(pipe, nullptr);
    const DWORD last = connected ? ERROR_SUCCESS : GetLastError();
    if (!connected && last != ERROR_PIPE_CONNECTED) {
      CloseHandle(pipe);
      pipe = INVALID_HANDLE_VALUE;
      continue;
    }
    if (g_stop.load()) {
      CloseHandle(pipe);
      pipe = INVALID_HANDLE_VALUE;
      break;
    }
    std::thread(HandleConnection, pipe).detach();
    pipe = INVALID_HANDLE_VALUE;
  }

  g_engine.Shutdown();
  if (mutex) CloseHandle(mutex);
  return 0;
}
