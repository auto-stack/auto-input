// AutoIME shared-engine dual-process probe (PLAN-078 T-02).
//
// Proves the cross-process learning contract that a single in-process librime
// adapter cannot satisfy (V-02/V-03/V-04 prerequisites):
//   * one owner process holds librime + autoime_user.userdb;
//   * two client processes on the same user data root learn and observe each
//     other's confirmations on the next composition, with isolated sessions;
//   * the owner can be killed and restarted without losing learnings, and a
//     client that loses the owner fails closed (no userdb of its own);
//   * a second owner refuses to start (single writable dictionary owner);
//   * warm key -> candidate-snapshot round trips are measured (P50/P95).
//
// Usage (harness mode, default):
//   rime_shared_probe.exe --engine <autoime_engine.exe> --resources-dir DIR
//                         [--self <rime_shared_probe.exe>] [--keep]
// Child mode (spawned by the harness):
//   rime_shared_probe.exe --client --pipe NAME --out FILE --mode learn|observe
//                         --code CODE [--expect TEXT]
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../engine/engine_client.h"
#include "json_mini.h"

namespace {

int g_checks = 0;
int g_failures = 0;
std::string g_results;

#define CHECK(cond)                                                     \
  do {                                                                  \
    ++g_checks;                                                         \
    if (!(cond)) {                                                      \
      ++g_failures;                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
    }                                                                   \
  } while (0)

long long NowUs() {
  static LARGE_INTEGER freq{};
  if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return freq.QuadPart ? now.QuadPart * 1000000 / freq.QuadPart : 0;
}

std::string Utf8FromWide(const std::wstring& wide) {
  if (wide.empty()) return std::string();
  const int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                    static_cast<int>(wide.size()), nullptr, 0,
                                    nullptr, nullptr);
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                      &out[0], n, nullptr, nullptr);
  return out;
}

std::wstring WideFromUtf8(const std::string& utf8) {
  if (utf8.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                    static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                      &out[0], n);
  return out;
}

std::string JsonEscape(const std::string& text) {
  std::string out;
  for (char c : text) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(c);
    } else if (static_cast<unsigned char>(c) < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04x", c);
      out += buf;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::vector<std::string> Utf8Args() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::string> args;
  for (int i = 0; i < argc; ++i) args.push_back(Utf8FromWide(argv[i]));
  if (argv) LocalFree(argv);
  return args;
}

struct Options {
  bool client = false;
  std::string script = "learn";  // child mode: learn | observe
  std::string engine;
  std::string resources_dir;
  std::string self;
  std::string pipe;
  std::string out;
  std::string code;
  std::string expect;
  bool keep = false;
};

bool ParseArgs(const std::vector<std::string>& args, Options* o,
               std::string* error) {
  for (size_t i = 1; i < args.size(); ++i) {
    const std::string& a = args[i];
    auto value = [&](std::string* out) {
      if (i + 1 >= args.size()) {
        *error = a + " needs a value";
        return false;
      }
      *out = args[++i];
      return true;
    };
    if (a == "--client") {
      o->client = true;
    } else if (a == "--engine") {
      if (!value(&o->engine)) return false;
    } else if (a == "--resources-dir") {
      if (!value(&o->resources_dir)) return false;
    } else if (a == "--self") {
      if (!value(&o->self)) return false;
    } else if (a == "--pipe") {
      if (!value(&o->pipe)) return false;
    } else if (a == "--out") {
      if (!value(&o->out)) return false;
    } else if (a == "--mode") {
      if (!value(&o->script)) return false;
    } else if (a == "--code") {
      if (!value(&o->code)) return false;
    } else if (a == "--expect") {
      if (!value(&o->expect)) return false;
    } else if (a == "--keep") {
      o->keep = true;
    } else {
      *error = "unknown argument: " + a;
      return false;
    }
  }
  return true;
}

bool WriteTextFile(const std::string& path, const std::string& text) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
  std::fclose(f);
  return ok;
}

bool ReadTextFile(const std::string& path, std::string* text) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  char buf[8192];
  size_t n = 0;
  text->clear();
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text->append(buf, n);
  const bool ok = !std::ferror(f);
  std::fclose(f);
  return ok;
}

// Mirrors the production KeyRouter: letters keep their case, uppercase letters
// carry the Shift modifier.
bool TypeString(autoime::EngineClient& client, uint64_t session,
                const std::string& keys, std::string* error) {
  for (char c : keys) {
    const bool upper = c >= 'A' && c <= 'Z';
    bool consumed = false;
    if (!client.Key(session, static_cast<int>(c), upper ? 1 : 0, &consumed,
                    error)) {
      return false;
    }
  }
  return true;
}

bool ReadPage(autoime::EngineClient& client, uint64_t session,
              autoime::RemoteSnapshot* snapshot, std::string* error) {
  return client.Context(session, snapshot, error);
}

int FindCandidate(const autoime::RemoteSnapshot& snapshot,
                  const std::wstring& text) {
  for (size_t i = 0; i < snapshot.candidates.size(); ++i) {
    if (snapshot.candidates[i] == text) return static_cast<int>(i);
  }
  return -1;
}

bool SelectText(autoime::EngineClient& client, uint64_t session,
                const std::wstring& text, std::string* committed,
                std::string* error) {
  autoime::RemoteSnapshot snapshot;
  if (!ReadPage(client, session, &snapshot, error)) return false;
  const int index = FindCandidate(snapshot, text);
  if (index < 0) {
    *error = "target not on the current page";
    return false;
  }
  bool selected = false;
  if (!client.Select(session, index, &selected, error)) return false;
  if (!selected) {
    *error = "engine refused the selection";
    return false;
  }
  bool has_commit = false;
  if (!client.Commit(session, committed, &has_commit, error)) return false;
  if (!has_commit) {
    *error = "selection produced no commit";
    return false;
  }
  return true;
}

struct Child {
  PROCESS_INFORMATION pi = {};
  HANDLE out_file = INVALID_HANDLE_VALUE;
  HANDLE err_file = INVALID_HANDLE_VALUE;
  bool running = false;
};

void CloseChildHandles(Child* child) {
  if (child->out_file != INVALID_HANDLE_VALUE) CloseHandle(child->out_file);
  if (child->err_file != INVALID_HANDLE_VALUE) CloseHandle(child->err_file);
  child->out_file = child->err_file = INVALID_HANDLE_VALUE;
}

bool SpawnChild(const std::string& exe, const std::string& command_line,
                const std::string& out_path, const std::string& err_path,
                Child* child, std::string* error) {
  SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
  HANDLE out = CreateFileW(WideFromUtf8(out_path).c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE err = CreateFileW(WideFromUtf8(err_path).c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (out == INVALID_HANDLE_VALUE || err == INVALID_HANDLE_VALUE) {
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (err != INVALID_HANDLE_VALUE) CloseHandle(err);
    *error = "cannot open child log files";
    return false;
  }
  STARTUPINFOW si = {sizeof(si)};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = out;
  si.hStdError = err;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  std::wstring cmdline = WideFromUtf8(command_line);
  std::vector<wchar_t> mutable_cmd(cmdline.begin(), cmdline.end());
  mutable_cmd.push_back(0);
  const BOOL ok = CreateProcessW(WideFromUtf8(exe).c_str(), mutable_cmd.data(),
                                 nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                                 &si, &child->pi);
  CloseHandle(out);
  CloseHandle(err);
  if (!ok) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "CreateProcess failed (%lu)", GetLastError());
    *error = buf;
    return false;
  }
  child->running = true;
  return true;
}

int WaitChild(Child* child, DWORD timeout_ms) {
  if (!child->running) return -1;
  const DWORD wait = WaitForSingleObject(child->pi.hProcess, timeout_ms);
  DWORD code = 0;
  if (wait == WAIT_OBJECT_0) {
    GetExitCodeProcess(child->pi.hProcess, &code);
  } else {
    TerminateProcess(child->pi.hProcess, 124);
    code = 124;
  }
  CloseHandle(child->pi.hThread);
  CloseHandle(child->pi.hProcess);
  child->running = false;
  return static_cast<int>(code);
}

std::string Quote(const std::string& value) {
  return "\"" + value + "\"";
}

// ---------------------------------------------------------------- child mode

int RunClient(const Options& o) {
  std::string error;
  autoime::EngineClient client;
  const std::wstring pipe = WideFromUtf8(o.pipe);
  if (!client.Connect(&error, 30000, pipe)) {
    std::printf("ERROR connect: %s\n", error.c_str());
    return 3;
  }
  if (!client.Hello(&error)) {
    std::printf("ERROR hello: %s\n", error.c_str());
    return 3;
  }
  uint64_t session = 0;
  if (!client.CreateSession(&session, &error)) {
    std::printf("ERROR create session: %s\n", error.c_str());
    return 3;
  }
  std::string json = "{\"role\":\"child\",\"code\":\"" + JsonEscape(o.code) +
                     "\",\"script\":\"" + JsonEscape(o.script) + "\"";
  if (o.script == "learn") {
    if (!client.Clear(session, &error) ||
        !TypeString(client, session, o.code, &error)) {
      std::printf("ERROR type: %s\n", error.c_str());
      return 3;
    }
    autoime::RemoteSnapshot snapshot;
    if (!ReadPage(client, session, &snapshot, &error)) {
      std::printf("ERROR context: %s\n", error.c_str());
      return 3;
    }
    if (snapshot.candidates.size() < 2) {
      std::printf("ERROR no second candidate to learn on '%s'\n",
                  o.code.c_str());
      return 3;
    }
    // Learn a candidate that is NOT already first, so the rank change is a real
    // observation instead of a pre-existing favourite.
    const std::wstring target = snapshot.candidates[1];
    int rank_before = 2;
    for (int i = 0; i < 4; ++i) {
      client.Clear(session, &error);
      if (!TypeString(client, session, o.code, &error)) break;
      std::string committed;
      if (!SelectText(client, session, target, &committed, &error)) {
        std::printf("ERROR learn select: %s\n", error.c_str());
        return 3;
      }
      if (committed != Utf8FromWide(target)) {
        std::printf("ERROR learn commit mismatch\n");
        return 3;
      }
      client.Confirm(session, &error);
    }
    client.Clear(session, &error);
    if (!TypeString(client, session, o.code, &error) ||
        !ReadPage(client, session, &snapshot, &error)) {
      std::printf("ERROR read back: %s\n", error.c_str());
      return 3;
    }
    const int rank_after = FindCandidate(snapshot, target) + 1;
    json += ",\"target\":\"" + JsonEscape(Utf8FromWide(target)) + "\"";
    json += ",\"rank_before\":" + std::to_string(rank_before);
    json += ",\"rank_after\":" + std::to_string(rank_after);
    json += ",\"first\":\"" +
            JsonEscape(snapshot.candidates.empty()
                           ? std::string()
                           : Utf8FromWide(snapshot.candidates[0])) +
            "\"";
  } else {
    const std::wstring expect = WideFromUtf8(o.expect);
    if (!client.Clear(session, &error) ||
        !TypeString(client, session, o.code, &error)) {
      std::printf("ERROR type: %s\n", error.c_str());
      return 3;
    }
    autoime::RemoteSnapshot snapshot;
    if (!ReadPage(client, session, &snapshot, &error)) {
      std::printf("ERROR context: %s\n", error.c_str());
      return 3;
    }
    const int rank = FindCandidate(snapshot, expect) + 1;
    json += ",\"target\":\"" + JsonEscape(o.expect) + "\"";
    json += ",\"rank\":" + std::to_string(rank);
    json += ",\"preedit\":\"" + JsonEscape(Utf8FromWide(snapshot.preedit_utf16)) + "\"";
  }
  json += ",\"pid\":" + std::to_string(GetCurrentProcessId()) + "}";
  client.DestroySession(session, &error);
  if (!o.out.empty() && !WriteTextFile(o.out, json)) {
    std::printf("ERROR cannot write result file %s\n", o.out.c_str());
    return 3;
  }
  std::printf("CLIENT %s\n", json.c_str());
  return 0;
}

// ------------------------------------------------------------- harness mode

std::string Number(double value) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.3f", value);
  return buf;
}

struct Report {
  Report() : last_us_(NowUs()) {}
  void Add(const std::string& name, bool pass, const std::string& detail) {
    const long long now = NowUs();
    const double ms = (now - last_us_) / 1000.0;
    last_us_ = now;
    if (!g_results.empty()) g_results += ",";
    g_results += "{\"name\":\"" + JsonEscape(name) + "\",\"pass\":" +
                 (pass ? "true" : "false") + ",\"ms\":" + Number(ms) +
                 ",\"detail\":\"" + JsonEscape(detail) + "\"}";
    std::printf("RESULT %s=%s ms=%.1f detail: %s\n", name.c_str(),
                pass ? "PASS" : "FAIL", ms, detail.c_str());
    if (!pass) ++g_failures;
  }

 private:
  long long last_us_;
};

int RunHarness(const Options& o) {
  std::string error;
  if (o.engine.empty()) {
    std::printf("ERROR --engine is required in harness mode\n");
    return 2;
  }
  std::string self = o.self;
  if (self.empty()) {
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    self = path;
  }
  char temp[MAX_PATH]{};
  GetTempPathA(MAX_PATH, temp);
  char unique[64];
  std::snprintf(unique, sizeof(unique), "autoime-shared-%lu-%llu",
                GetCurrentProcessId(),
                static_cast<unsigned long long>(GetTickCount64()));
  const std::string root = std::string(temp) + unique;
  const std::string user_dir = root + "\\user";
  const std::string scratch = root + "\\evidence";
  CreateDirectoryA(root.c_str(), nullptr);
  CreateDirectoryA(user_dir.c_str(), nullptr);
  CreateDirectoryA(scratch.c_str(), nullptr);
  const std::string server_out = scratch + "\\engine.out.log";
  const std::string server_err = scratch + "\\engine.err.log";
  const std::string child_out = scratch + "\\child.out.log";
  const std::string child_err = scratch + "\\child.err.log";
  const std::string child_json = scratch + "\\child.json";
  const std::string child2_json = scratch + "\\child2.json";

  // PLAN-078 review N-02: isolate the harness from the installed engine.
  std::string instance;
  if (!autoime::ProfileInstanceForUserDir(user_dir, &instance, &error)) {
    std::printf("ERROR %s\n", error.c_str());
    return 2;
  }
  std::wstring pipe_name;
  if (!autoime::UserPipeName(instance, &pipe_name, &error)) {
    return 2;
  }
  const std::string pipe = Utf8FromWide(pipe_name);
  const std::string engine_cmd =
      Quote(o.engine) + " --serve --instance " + Quote(instance) + " --pipe " + Quote(pipe) +
      " --resources-dir " + Quote(o.resources_dir) + " --user-data-dir " +
      Quote(user_dir);

  Report report;
  Child server;
  if (!SpawnChild(o.engine, engine_cmd, server_out, server_err, &server,
                  &error)) {
    std::printf("ERROR spawn engine: %s\n", error.c_str());
    return 2;
  }
  const long long startup_start = NowUs();
  autoime::EngineClient client;
  bool connected = false;
  for (int attempt = 0; attempt < 200 && !connected; ++attempt) {
    std::string connect_error;
    if (client.Connect(&connect_error, 200, pipe_name)) {
      connected = client.Hello(&connect_error);
      if (!connected) {
        std::printf("HELLO failed: %s\n", connect_error.c_str());
        break;
      }
    } else {
      Sleep(150);
    }
  }
  const double startup_ms = (NowUs() - startup_start) / 1000.0;
  report.Add("engine_start_and_hello", connected,
             "connected=" + std::string(connected ? "1" : "0") +
                 ", startup_ms=" + Number(startup_ms));
  if (!connected) return 5;

  uint64_t session = 0;
  CHECK(client.CreateSession(&session, &error));

  // Baseline ranking of a word the harness will teach: 李显 on "lixian" is
  // rank 2 on an empty user dictionary (probe t12 baseline).
  autoime::RemoteSnapshot snapshot;
  CHECK(client.Clear(session, &error));
  CHECK(TypeString(client, session, "lixian", &error));
  CHECK(ReadPage(client, session, &snapshot, &error));
  const std::wstring lixian = L"李显";
  const int lixian_before = FindCandidate(snapshot, lixian) + 1;
  report.Add("baseline_lixian_rank", lixian_before > 1,
             "rank=" + std::to_string(lixian_before) +
                 " (needs >1 to prove learning)");

  int learned = 0;
  for (int i = 0; i < 3 && lixian_before > 0; ++i) {
    // Each confirmation is a fresh composition: clear, type, select the target
    // by its text, and take exactly one commit.
    client.Clear(session, &error);
    if (!TypeString(client, session, "lixian", &error)) break;
    std::string committed;
    if (SelectText(client, session, lixian, &committed, &error) &&
        committed == Utf8FromWide(lixian)) {
      ++learned;
    }
  }
  report.Add("owner_process_learns", learned == 3,
             "committed=" + std::to_string(learned) + "/3");

  // A second session in the SAME process sees the learning immediately.
  uint64_t session2 = 0;
  CHECK(client.CreateSession(&session2, &error));
  CHECK(client.Clear(session2, &error));
  CHECK(TypeString(client, session2, "lixian", &error));
  CHECK(ReadPage(client, session2, &snapshot, &error));
  const int lixian_after = FindCandidate(snapshot, lixian) + 1;
  report.Add("same_process_second_session_sees_learning", lixian_after == 1,
             "rank=" + std::to_string(lixian_after));

  // A separate process must see it too (child observe).
  const std::string observe_cmd =
      Quote(self) + " --client --pipe " + Quote(pipe) + " --out " +
      Quote(child_json) + " --mode observe --code lixian --expect " +
      Quote(Utf8FromWide(lixian));
  Child child;
  bool child_ok = false;
  if (SpawnChild(self, observe_cmd, child_out, child_err, &child, &error)) {
    const int code = WaitChild(&child, 60000);
    std::string json;
    if (code == 0 && ReadTextFile(child_json, &json)) {
      json_mini::Value doc;
      std::string parse_error;
      if (json_mini::Parse(json, &doc, &parse_error)) {
        const int rank = static_cast<int>(doc.member("rank")
                                              ? doc.member("rank")->number
                                              : 0);
        child_ok = rank == 1;
        report.Add("other_process_sees_owner_learning", child_ok,
                   "rank=" + std::to_string(rank) + " exit=" +
                       std::to_string(code));
      }
    }
    if (!child_ok) {
      report.Add("other_process_sees_owner_learning", false,
                 "child exit=" + std::to_string(code));
    }
  } else {
    report.Add("other_process_sees_owner_learning", false, error);
  }

  // Isolation: while this client composes "ni", another process types.
  CHECK(client.Clear(session, &error));
  CHECK(TypeString(client, session, "ni", &error));
  CHECK(ReadPage(client, session, &snapshot, &error));
  const std::string preedit_before = Utf8FromWide(snapshot.preedit_utf16);
  const size_t candidates_before = snapshot.candidates.size();

  const std::string learn_cmd =
      Quote(self) + " --client --pipe " + Quote(pipe) + " --out " +
      Quote(child2_json) + " --mode learn --code shijie";
  Child learner;
  std::string learned_target;
  int learned_rank_before = 0;
  int learned_rank_after = 0;
  bool learner_ok = false;
  if (SpawnChild(self, learn_cmd, child_out, child_err, &learner, &error)) {
    const int code = WaitChild(&learner, 60000);
    std::string json;
    if (code == 0 && ReadTextFile(child2_json, &json)) {
      json_mini::Value doc;
      std::string parse_error;
      if (json_mini::Parse(json, &doc, &parse_error)) {
        learned_target = doc.str_or("target", "");
        learned_rank_before =
            static_cast<int>(doc.member("rank_before")
                                 ? doc.member("rank_before")->number
                                 : 0);
        learned_rank_after =
            static_cast<int>(doc.member("rank_after")
                                 ? doc.member("rank_after")->number
                                 : 0);
        learner_ok = learned_rank_before == 2 && learned_rank_after == 1;
      }
    }
    report.Add("second_process_learns_independently", learner_ok,
               "target=" + learned_target + " rank " +
                   std::to_string(learned_rank_before) + "->" +
                   std::to_string(learned_rank_after) + " exit=" +
                   std::to_string(code));
  } else {
    report.Add("second_process_learns_independently", false, error);
  }

  // This client's composition survived the other process's activity.
  CHECK(ReadPage(client, session, &snapshot, &error));
  report.Add(
      "composition_isolated_across_clients",
      Utf8FromWide(snapshot.preedit_utf16) == preedit_before &&
          snapshot.candidates.size() == candidates_before && !preedit_before.empty(),
      "preedit='" + Utf8FromWide(snapshot.preedit_utf16) + "' candidates=" +
          std::to_string(snapshot.candidates.size()));
  CHECK(client.Clear(session, &error));

  // Bidirectional: the owner sees the other process's learning on the next
  // composition.
  bool owner_sees_child = false;
  int owner_rank = 0;
  if (!learned_target.empty()) {
    CHECK(TypeString(client, session, "shijie", &error));
    CHECK(ReadPage(client, session, &snapshot, &error));
    owner_rank = FindCandidate(snapshot, WideFromUtf8(learned_target)) + 1;
    owner_sees_child = owner_rank == 1;
    CHECK(client.Clear(session, &error));
  }
  report.Add("owner_sees_other_process_learning", owner_sees_child,
             "target=" + learned_target + " rank=" +
                 std::to_string(owner_rank));

  // PLAN-078 T-02 confirmation contract: the engine learned at confirm time, so
  // a host edit failure must be compensated with Forget. The learned entry is
  // removed again and the ranking returns to the system baseline.
  const bool confirmed = client.Confirm(session, &error);
  const bool forgotten =
      // PLAN-078 review F-01: production forgets without resending the code.
      client.Forget(session, "", Utf8FromWide(lixian), &error);
  CHECK(client.Clear(session, &error));
  CHECK(TypeString(client, session, "lixian", &error));
  CHECK(ReadPage(client, session, &snapshot, &error));
  const int lixian_after_forget = FindCandidate(snapshot, lixian) + 1;
  report.Add("failed_host_edit_is_forgotten", confirmed && forgotten &&
                                                  lixian_after_forget > 1,
             "confirm=" + std::string(confirmed ? "1" : "0") +
                 " forget=" + std::string(forgotten ? "1" : "0") +
                 " rank=" + std::to_string(lixian_after_forget));
  // Restore the learning used by the restart checks below.
  for (int i = 0; i < 3; ++i) {
    client.Clear(session, &error);
    if (!TypeString(client, session, "lixian", &error)) break;
    std::string committed;
    if (SelectText(client, session, lixian, &committed, &error) &&
        committed == Utf8FromWide(lixian)) {
      client.Confirm(session, &error);
    }
  }

  // Warm key -> candidate snapshot latency over the IPC path.
  CHECK(TypeString(client, session, "n", &error));
  CHECK(ReadPage(client, session, &snapshot, &error));
  std::vector<double> samples;
  for (int i = 0; i < 220; ++i) {
    CHECK(client.Clear(session, &error));
    const long long start = NowUs();
    bool consumed = false;
    CHECK(client.Key(session, static_cast<int>('n'), 0, &consumed, &error));
    CHECK(ReadPage(client, session, &snapshot, &error));
    const long long elapsed = NowUs() - start;
    if (i >= 20) samples.push_back(elapsed / 1000.0);
  }
  CHECK(client.Clear(session, &error));
  std::sort(samples.begin(), samples.end());
  const double p50 = samples.empty() ? 0 : samples[samples.size() / 2];
  const double p95 =
      samples.empty() ? 0 : samples[static_cast<size_t>(samples.size() * 0.95)];
  report.Add("ipc_key_to_snapshot_latency", p95 < 10.0,
             "n=" + std::to_string(samples.size()) + " p50=" + Number(p50) +
                 "ms p95=" + Number(p95) + "ms (limit 10ms)");

  // Recovery: kill the owner; the client fails closed, never opens its own
  // database. Then a restarted owner must still know both learnings.
  TerminateProcess(server.pi.hProcess, 9);
  WaitChild(&server, 10000);
  uint64_t ping = 0;
  const bool ping_after_kill = client.Ping(&ping, &error);
  report.Add("client_fails_closed_when_owner_dies", !ping_after_kill,
             "ping_ok=" + std::string(ping_after_kill ? "1" : "0"));
  client.Close();

  Child server2;
  if (!SpawnChild(o.engine, engine_cmd, server_out, server_err, &server2,
                  &error)) {
    report.Add("owner_restart", false, error);
  } else {
    bool reconnected = false;
    for (int attempt = 0; attempt < 200 && !reconnected; ++attempt) {
      std::string connect_error;
      if (client.Connect(&connect_error, 200, pipe_name)) {
        reconnected = client.Hello(&connect_error);
      } else {
        Sleep(150);
      }
    }
    report.Add("owner_restart", reconnected, "reconnected=" + std::string(reconnected ? "1" : "0"));
    if (reconnected) {
      uint64_t s3 = 0;
      CHECK(client.CreateSession(&s3, &error));
      CHECK(client.Clear(s3, &error));
      CHECK(TypeString(client, s3, "lixian", &error));
      CHECK(ReadPage(client, s3, &snapshot, &error));
      const int persisted = FindCandidate(snapshot, lixian) + 1;
      report.Add("learning_survives_owner_restart", persisted == 1,
                 "lixian rank=" + std::to_string(persisted));
      bool child_persisted = false;
      int child_rank = 0;
      if (!learned_target.empty()) {
        CHECK(client.Clear(s3, &error));
        CHECK(TypeString(client, s3, "shijie", &error));
        CHECK(ReadPage(client, s3, &snapshot, &error));
        child_rank = FindCandidate(snapshot, WideFromUtf8(learned_target)) + 1;
        child_persisted = child_rank == 1;
      }
      report.Add("other_process_learning_survives_restart", child_persisted,
                 "target=" + learned_target + " rank=" +
                     std::to_string(child_rank));
      // Single owner: a second engine must refuse while this one runs.
      Child duplicate;
      const std::string dup_cmd =
          Quote(o.engine) + " --serve --instance " + Quote(instance) + " --pipe " + Quote(pipe) +
          " --resources-dir " + Quote(o.resources_dir) + " --user-data-dir " +
          Quote(user_dir);
      const std::string dup_out = scratch + "\\dup.out.log";
      const std::string dup_err = scratch + "\\dup.err.log";
      if (SpawnChild(o.engine, dup_cmd, dup_out, dup_err, &duplicate,
                     &error)) {
        const int code = WaitChild(&duplicate, 20000);
        report.Add("single_owner_guard", code == 8,
                   "duplicate exit=" + std::to_string(code));
      } else {
        report.Add("single_owner_guard", false, error);
      }
      std::string shutdown_error;
      const bool shutdown_ok = client.Shutdown(&shutdown_error);
      WaitChild(&server2, 10000);
      report.Add("graceful_shutdown", shutdown_ok, shutdown_error);
    }
  }

  // No client ever opened the writable dictionary: the owner log must not show
  // a lock failure for autoime_user.userdb.
  std::string server_log;
  ReadTextFile(server_err, &server_log);
  const bool lock_failure = server_log.find("LOCK") != std::string::npos;
  report.Add("no_userdb_lock_failure", !lock_failure,
             lock_failure ? "engine log contains LOCK" : "engine log clean");

  std::string summary = "{\"harness\":\"rime_shared_probe\",\"checks\":" +
                        std::to_string(g_checks) + ",\"failures\":" +
                        std::to_string(g_failures) + ",\"startup_ms\":" +
                        Number(startup_ms) + ",\"results\":[" + g_results +
                        "]}";
  std::printf("SUMMARY %s\n", summary.c_str());
  if (!o.keep) {
    // Remove the scratch tree (files are closed by now).
    std::string cmd = "cmd /c rmdir /s /q " + Quote(root);
    std::system(cmd.c_str());
  } else {
    std::printf("KEEP %s\n", root.c_str());
  }
  return g_failures == 0 ? 0 : 5;
}

}  // namespace

int main() {
  SetConsoleOutputCP(CP_UTF8);
  const std::vector<std::string> args = Utf8Args();
  Options options;
  std::string error;
  if (!ParseArgs(args, &options, &error)) {
    std::printf("ERROR bad usage: %s\n", error.c_str());
    return 2;
  }
  if (options.client) return RunClient(options);
  return RunHarness(options);
}
