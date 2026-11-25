// AutoIME deployment CLI (PLAN-070 T-04; phrase transactions PLAN-072 T-03).
//
// Two modes:
//  1. Plain maintenance (historical): synchronously deploys the shipped Rime
//     resources into AutoIME's private user dir, compiles them, validates the
//     effective schema (last known-good rollback, Luna fallback), measures
//     first-deploy and ready-state latency, prints one JSON line.
//     Exit codes: 0 ready, 2 rime.dll unavailable, 3 engine disabled,
//     5 Luna fallback, 6 bad usage.
//  2. `phrases apply --request <file>`: the phrase transaction. The manager
//     stages a complete custom_phrase.txt and the exact source bytes it read;
//     this mode re-checks the source under the deployment lock, publishes the
//     staged file, runs maintenance, verifies the expected candidate in a NEW
//     Rime session, and rolls back on any failure. Exit codes: 0 ok, 4 io,
//     5 conflict (re-read and retry), 6 deploy/verification failure (previous
//     version restored), 7 busy (retry).
//
// Diagnostics are payload-free: no phrase text or codes are printed.
#include "../deployment.h"
#include "../diag.h"
#include "../../engine/engine_client.h"
#include "../rime_engine.h"

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// The library calls ImeLog for structural diagnostics; this CLI writes them to
// stderr only (payload-free by contract).
void ImeLog(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::vfprintf(stderr, fmt, args);
  va_end(args);
  std::fputc('\n', stderr);
}

namespace {

std::string Utf8Of(const std::wstring& wide) {
  if (wide.empty()) return std::string();
  const int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                    static_cast<int>(wide.size()), nullptr, 0,
                                    nullptr, nullptr);
  std::string out(static_cast<size_t>(n), '\0');
  if (n > 0)
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                        static_cast<int>(wide.size()), &out[0], n, nullptr,
                        nullptr);
  return out;
}

std::wstring WideOf(const std::string& utf8) {
  if (utf8.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                    static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(n), L'\0');
  if (n > 0)
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                        static_cast<int>(utf8.size()), &out[0], n);
  return out;
}

std::string ExeDir() {
  wchar_t path[32768]{};
  const DWORD n = GetModuleFileNameW(nullptr, path, 32768);
  std::string dir = Utf8Of(std::wstring(path, n));
  const size_t slash = dir.find_last_of("\\/");
  if (slash != std::string::npos) dir.resize(slash);
  return dir;
}

long long NowUs() {
  static LARGE_INTEGER freq{};
  if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return freq.QuadPart ? now.QuadPart * 1000000 / freq.QuadPart : 0;
}

struct Latency {
  int samples = 0;
  long long median_us = 0;
  long long p95_us = 0;
  long long max_us = 0;
};

// Time from the first key of "nihao" to a non-empty candidate page, on a
// warmed engine (first two iterations are warm-up only).
Latency MeasureLatency(RimeEngine* engine, uintptr_t session, int samples) {
  Latency result;
  if (!session || samples <= 0) return result;
  std::vector<long long> times;
  for (int i = 0; i < samples + 2; ++i) {
    engine->ClearComposition(session);
    const long long start = NowUs();
    const char keys[] = "nihao";
    for (const char* p = keys; *p; ++p) {
      engine->ProcessKey(session, *p, 0);
    }
    RimeEngine::Snapshot snap;
    bool have = false;
    if (engine->GetContext(session, &snap) && !snap.candidates.empty())
      have = true;
    const long long elapsed = NowUs() - start;
    if (have && i >= 2) times.push_back(elapsed);
    engine->ClearComposition(session);
  }
  engine->ClearComposition(session);
  if (times.empty()) return result;
  std::sort(times.begin(), times.end());
  result.samples = static_cast<int>(times.size());
  result.median_us = times[times.size() / 2];
  const size_t p95_index =
      static_cast<size_t>(0.95 * (times.size() - 1));
  result.p95_us = times[p95_index];
  result.max_us = times.back();
  return result;
}

std::string JsonEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(c);
    } else if (c == '\n' || c == '\r' || c == '\t') {
      out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  return out;
}

// Case-insensitive path identity, used to decide whether a running owner serves
// the same user data root as this invocation.
bool SamePath(const std::string& a, const std::string& b) {
  auto normalize = [](std::string value) {
    for (char& c : value) {
      if (c == '/') c = '\\';
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    while (!value.empty() && value.back() == '\\') value.pop_back();
    return value;
  };
  return normalize(a) == normalize(b);
}

bool ReadUtf8File(const std::string& path, std::string* out) {
  const std::wstring wide = WideOf(path);
  HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  out->clear();
  char buffer[4096];
  DWORD n = 0;
  while (ReadFile(h, buffer, sizeof(buffer), &n, nullptr) && n > 0)
    out->append(buffer, n);
  CloseHandle(h);
  return true;
}

// One verification row: ("present"|"absent") TAB code TAB text (UTF-8).
struct VerifyRow {
  bool present = true;
  std::string code;
  std::wstring text;
};

bool LoadVerifyRows(const std::string& path, std::vector<VerifyRow>* rows,
                    std::string* error) {
  std::string body;
  if (!ReadUtf8File(path, &body)) {
    *error = "verification file unreadable";
    return false;
  }
  size_t pos = 0;
  while (pos < body.size()) {
    const size_t nl = body.find('\n', pos);
    std::string line = body.substr(pos, nl == std::string::npos ? std::string::npos
                                                               : nl - pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) {
      const size_t t1 = line.find('\t');
      const size_t t2 = t1 == std::string::npos ? std::string::npos
                                                : line.find('\t', t1 + 1);
      if (t1 == std::string::npos || t2 == std::string::npos) {
        *error = "malformed verification row";
        return false;
      }
      VerifyRow row;
      const std::string mode = line.substr(0, t1);
      row.present = mode == "present";
      if (!row.present && mode != "absent") {
        *error = "unknown verification mode";
        return false;
      }
      row.code = line.substr(t1 + 1, t2 - t1 - 1);
      row.text = WideOf(line.substr(t2 + 1));
      if (row.code.empty() || row.text.empty()) {
        *error = "empty verification row";
        return false;
      }
      rows->push_back(row);
    }
    if (nl == std::string::npos) break;
    pos = nl + 1;
  }
  return true;
}

// A NEW engine session must see (present) or not see (absent) the phrase after
// typing its code. This is the "new session loaded the right schema and the
// phrase is visible" oracle; fallback/disabled states never report success.
bool VerifyCandidates(RimeEngine* engine, const std::string& verify_file,
                      std::string* error) {
  std::vector<VerifyRow> rows;
  if (!LoadVerifyRows(verify_file, &rows, error)) return false;
  if (rows.empty()) return true;
  for (const VerifyRow& row : rows) {
    const uintptr_t session = engine->CreateSession();
    if (!session) {
      *error = "cannot create verification session";
      return false;
    }
    for (char c : row.code) {
      engine->ProcessKey(session, static_cast<unsigned char>(c), 0);
    }
    // R-02 repair: the new-session oracle must cover EVERY page of the code's
    // candidate list, not only the visible page. librime exposes one page per
    // context read, so walk the pages with the Page_Down keysym (the IME routes
    // VK_NEXT to 0xff56) and collect each page's candidates.
    bool found = false;
    bool cancelled = false;
    for (int page = 0; page < 20 && !cancelled; ++page) {
      RimeEngine::Snapshot snap;
      if (!engine->GetContext(session, &snap)) break;
      for (const std::wstring& candidate : snap.candidates) {
        if (std::getenv("AUTOIME_DEBUG_VERIFY")) {
          std::fprintf(stderr, "verify candidate p%d: %s\n", page,
                       Utf8Of(candidate).c_str());
        }
        if (candidate == row.text) {
          found = true;
          cancelled = true;
          break;
        }
      }
      if (cancelled || snap.is_last_page) break;
      if (!engine->ProcessKey(session, 0xff56, 0)) break;  // XK_Page_Down
    }
    engine->ClearComposition(session);
    engine->DestroySession(session);
    if (found != row.present) {
      *error = row.present ? "verification failed: phrase not visible"
                           : "verification failed: phrase still visible";
      return false;
    }
  }
  return true;
}

int RunPhraseApply(const std::string& request_path) {
  deployment::PhraseRequest req;
  std::string error;
  if (!deployment::ReadPhraseRequest(request_path, &req, &error)) {
    std::printf("{\"phrase_apply\": {\"code\": 4, \"detail\": \"%s\"}}\n",
                JsonEscape(error).c_str());
    return 4;
  }
  const std::string module_dir = ExeDir();
  const std::string resources_dir = deployment::ResolveResourcesDir(module_dir);
  deployment::PhraseResult result = deployment::ApplyPhraseFile(
      request_path, [&](std::string* maintain_error) -> bool {
        RimeEngine engine;
        engine.SetModuleDir(module_dir);
        engine.SetPaths(resources_dir, req.user_dir);
        std::string ready_error;
        if (!engine.EnsureReady(&ready_error)) {
          *maintain_error = ready_error.empty() ? "engine not ready" : ready_error;
          engine.Shutdown();
          return false;
        }
        if (engine.fallback() || engine.disabled() || !engine.ready()) {
          *maintain_error = "engine not in the primary ready state";
          engine.Shutdown();
          return false;
        }
        if (!req.verify_file.empty() && !req.verify_file.empty()) {
          std::string verify_error;
          if (!VerifyCandidates(&engine, req.verify_file, &verify_error)) {
            *maintain_error = verify_error;
            engine.Shutdown();
            return false;
          }
        }
        engine.Shutdown();
        return true;
      });
  std::printf(
      "{\"phrase_apply\": {\"code\": %d, \"conflict\": %d, \"busy\": %d, "
      "\"rolled_back\": %d, \"recovered\": %d, \"before\": \"%s\", "
      "\"after\": \"%s\", \"detail\": \"%s\"}}\n",
      result.code, result.conflict ? 1 : 0, result.busy ? 1 : 0,
      result.rolled_back ? 1 : 0, result.recovered ? 1 : 0,
      JsonEscape(result.before_version).c_str(),
      JsonEscape(result.after_version).c_str(),
      JsonEscape(result.detail).c_str());
  std::fflush(stdout);
  std::_Exit(result.code);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 2 && std::strcmp(argv[1], "phrases") == 0) {
    if (std::strcmp(argv[2], "apply") == 0) {
      if (argc != 5 || std::strcmp(argv[3], "--request") != 0) {
        std::fprintf(stderr,
                     "usage: autoime_deploy phrases apply --request <file>\n");
        return 6;
      }
      return RunPhraseApply(argv[4]);
    }
    std::fprintf(stderr, "unknown phrases command: %s\n", argv[2]);
    return 6;
  }

  std::string resources_dir;
  std::string user_dir;
  int latency_samples = 0;
  bool cleanup_legacy = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&]() -> const char* {
      if (i + 1 >= argc) return nullptr;
      return argv[++i];
    };
    if (arg == "--resources-dir") {
      const char* v = value();
      if (!v) return 6;
      resources_dir = v;
    } else if (arg == "--user-data-dir") {
      const char* v = value();
      if (!v) return 6;
      user_dir = v;
    } else if (arg == "--latency") {
      const char* v = value();
      if (!v) return 6;
      latency_samples = std::atoi(v);
    } else if (arg == "--cleanup-legacy-log") {
      cleanup_legacy = true;
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
      return 6;
    }
  }

  if (cleanup_legacy) {
    std::string path;
    const bool removed = deployment::CleanupLegacyLog(&path);
    std::printf("{\"legacy_log\": \"%s\", \"removed\": %d}\n",
                JsonEscape(path).c_str(), removed ? 1 : 0);
    return 0;
  }

  const std::string module_dir = ExeDir();
  if (resources_dir.empty())
    resources_dir = deployment::ResolveResourcesDir(module_dir);
  if (user_dir.empty()) user_dir = deployment::DefaultUserDir();

  // PLAN-078 T-03: when the per-user owner is already serving this data root it
  // holds the writable dictionary and has the compiled files mapped, so a
  // second process must not replace them. Ask the owner to redeploy instead.
  {
    autoime::EngineClient owner;
    std::string owner_error;
    // Match the owner that serves THIS data root (the default profile has the
    // un-suffixed owner; alternate roots use a profile-specific instance).
    std::string instance;
    std::wstring pipe_name;
    const bool serve_by_owner =
        autoime::ProfileInstanceForUserDir(user_dir, &instance, nullptr) &&
        autoime::UserPipeName(instance, &pipe_name, &owner_error) &&
        owner.Connect(&owner_error, 200, pipe_name) &&
        owner.Hello(&owner_error) && !user_dir.empty() &&
        SamePath(owner.user_data_dir(), user_dir);
    if (serve_by_owner) {
      autoime::EngineClient::DeployResult result;
      std::string deploy_error;
      const long long owner_start = NowUs();
      if (owner.Deploy(resources_dir, user_dir, &result, &deploy_error)) {
        const long long deploy_ms = (NowUs() - owner_start) / 1000;
        const std::string error = result.error + deploy_error;
        std::printf(
            "{\"deploy_ms\": %lld, \"ready\": %d, \"fallback\": %d, "
            "\"disabled\": %d, \"schema\": \"%s\", \"resources_dir\": \"%s\", "
            "\"user_data_dir\": \"%s\", \"resources_version\": \"%s\", "
            "\"detail\": \"%s\", \"latency_n\": 0, \"median_us\": 0, "
            "\"p95_us\": 0, \"max_us\": 0, \"via\": \"owner\", "
            "\"error\": \"%s\"}\n",
            deploy_ms, result.ok ? 1 : 0, result.fallback ? 1 : 0,
            result.disabled ? 1 : 0, JsonEscape(result.schema).c_str(),
            JsonEscape(resources_dir).c_str(), JsonEscape(user_dir).c_str(),
            JsonEscape(result.version).c_str(),
            JsonEscape(result.detail).c_str(), JsonEscape(error).c_str());
        std::fflush(stdout);
        const int owner_code =
            !result.ok ? (result.disabled ? 3 : 2) : (result.fallback ? 5 : 0);
        std::_Exit(owner_code);
      }
    }
  }

  RimeEngine engine;
  engine.SetModuleDir(module_dir);
  engine.SetPaths(resources_dir, user_dir);

  // Settle any phrase transaction a dead process left pending before the
  // engine opens the user data dir.
  std::string recovery_detail;
  if (deployment::RecoverPendingPhrase(user_dir, &recovery_detail)) {
    std::fprintf(stderr, "phrase recovery: %s\n", recovery_detail.c_str());
  }
  const long long deploy_start = NowUs();
  std::string error;
  const bool ok = engine.EnsureReady(&error);
  const long long deploy_ms = (NowUs() - deploy_start) / 1000;

  Latency latency;
  if (ok) {
    const uintptr_t session = engine.CreateSession();
    if (session) {
      latency = MeasureLatency(&engine, session, latency_samples);
      engine.DestroySession(session);
    }
  }

  const bool fallback = engine.fallback();
  std::printf(
      "{\"deploy_ms\": %lld, \"ready\": %d, \"fallback\": %d, "
      "\"disabled\": %d, \"schema\": \"%s\", \"resources_dir\": \"%s\", "
      "\"user_data_dir\": \"%s\", \"resources_version\": \"%s\", "
      "\"detail\": \"%s\", \"latency_n\": %d, \"median_us\": %lld, "
      "\"p95_us\": %lld, \"max_us\": %lld, \"error\": \"%s\"}\n",
      deploy_ms, ok ? 1 : 0, fallback ? 1 : 0, engine.disabled() ? 1 : 0,
      JsonEscape(engine.schema()).c_str(), JsonEscape(resources_dir).c_str(),
      JsonEscape(user_dir).c_str(),
      JsonEscape(engine.deployment_version()).c_str(),
      JsonEscape(engine.deployment_detail()).c_str(), latency.samples,
      latency.median_us, latency.p95_us, latency.max_us,
      JsonEscape(error).c_str());
  const int code = !ok ? (engine.disabled() ? 3 : 2) : (fallback ? 5 : 0);
  engine.Shutdown();
  std::fflush(stdout);
  // rime.dll keeps background state whose CRT teardown is unreliable; the
  // engine has been finalized above, so exit directly.
  std::_Exit(code);
}
