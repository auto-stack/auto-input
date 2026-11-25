// AutoIME - resource deployment, recovery, and phrase transactions
// implementation (PLAN-070 T-04; PLAN-072 T-03).
#include "deployment.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "diag.h"

namespace deployment {
namespace {

const char* const kManaged[] = {
    "autoime_pinyin.schema.yaml",
    "autoime_terms.schema.yaml",
    "autoime_en.schema.yaml",
    "autoime_words.schema.yaml",
    // PLAN-078 T-05: the dependency schema must be deployed so the deployer
    // compiles the personal term table; the table itself stays user-owned
    // (kUserTerms below).
    "autoime_user_terms.schema.yaml",
    "autoime_terms.dict.yaml",
    "autoime_en.dict.yaml",
    "autoime_words.dict.yaml",
};
const char* const kPhrase = "custom_phrase.txt";
// PLAN-078 T-05: personal English terms. User-owned like custom_phrase.txt:
// copied only when absent, never overwritten and never part of the resource
// version hash (the manager owns the content; deployment compiles it).
const char* const kUserTerms = "autoime_user_terms.dict.yaml";
const char* const kMarker = ".autoime-resources";
const char* const kPhraseMarker = ".autoime-phrase-version";
const char* const kPending = ".autoime-resources.pending";
const char* const kBackupDir = ".autoime-backup";
const char* const kStageDir = ".autoime-staging";
const char* const kBuildDir = "build";
// Phrase transaction recovery records (single transaction at a time; both live
// in the user data dir and are removed when the transaction settles).
const char* const kTxnJournal = ".autoime-phrase-txn";
const char* const kTxnBackup = ".autoime-phrase-backup";

// ---- UTF-8 <-> UTF-16 boundary -------------------------------------------

std::wstring Wide(const std::string& utf8) {
  if (utf8.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                    static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(n), L'\0');
  if (n > 0)
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                        static_cast<int>(utf8.size()), &out[0], n);
  return out;
}

std::string Utf8(const std::wstring& wide) {
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

std::string Join(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  if (b[0] == '\\' || b[0] == '/') return b;
  if (a.back() == '\\' || a.back() == '/') return a + b;
  return a + "\\" + b;
}

bool FileExists(const std::string& path) {
  const DWORD attrs = GetFileAttributesW(Wide(path).c_str());
  return attrs != INVALID_FILE_ATTRIBUTES &&
         !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::string& path) {
  const DWORD attrs = GetFileAttributesW(Wide(path).c_str());
  return attrs != INVALID_FILE_ATTRIBUTES &&
         (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool EnsureDir(const std::string& path) {
  if (DirExists(path)) return true;
  CreateDirectoryW(Wide(path).c_str(), nullptr);
  return DirExists(path);
}

uint64_t Fnv1a(const char* data, size_t size) {
  uint64_t hash = 1469598103934665603ULL;
  for (size_t i = 0; i < size; ++i) {
    hash ^= static_cast<unsigned char>(data[i]);
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool ReadAll(const std::string& path, std::string* out) {
  HANDLE h = CreateFileW(Wide(path).c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  out->clear();
  char buffer[4096];
  DWORD n = 0;
  while (ReadFile(h, buffer, sizeof(buffer), &n, nullptr) && n > 0) {
    out->append(buffer, n);
  }
  CloseHandle(h);
  return true;
}

bool WriteAll(const std::string& path, const std::string& data) {
  const std::string tmp = path + ".tmp";
  HANDLE h = CreateFileW(Wide(tmp).c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  bool wrote = true;
  size_t off = 0;
  while (off < data.size()) {
    const DWORD chunk = static_cast<DWORD>(
        data.size() - off > 65536 ? 65536 : data.size() - off);
    DWORD n = 0;
    if (!WriteFile(h, data.data() + off, chunk, &n, nullptr) || n == 0) {
      wrote = false;
      break;
    }
    off += n;
  }
  if (wrote) FlushFileBuffers(h);
  CloseHandle(h);
  if (!wrote) {
    DeleteFileW(Wide(tmp).c_str());
    return false;
  }
  return MoveFileExW(Wide(tmp).c_str(), Wide(path).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::string Env(const char* name) {
  wchar_t buffer[32768];
  const DWORD n = GetEnvironmentVariableW(Wide(name).c_str(), buffer,
                                          static_cast<DWORD>(32768));
  if (n == 0 || n >= 32768) return std::string();
  return Utf8(std::wstring(buffer, n));
}

std::string VersionString(uint64_t digest) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "r%016llx",
                static_cast<unsigned long long>(digest));
  return buffer;
}

std::string MarkerPath(const std::string& dir) { return Join(dir, kMarker); }
std::string PhraseMarkerPath(const std::string& dir) {
  return Join(dir, kPhraseMarker);
}
std::string PhrasePath(const std::string& dir) { return Join(dir, kPhrase); }

// PLAN-078 T-05: personal data identity covers BOTH user-owned files. The
// personal-terms table is compiled, so a changed terms file must invalidate the
// "resources unchanged" fast path exactly like custom_phrase.txt does; without
// this an added term was written but the deployed table stayed stale.
std::string CurrentPhraseVersion(const std::string& user_dir) {
  std::string phrase;
  std::string terms;
  bool any = false;
  std::string combined;
  if (ReadAll(PhrasePath(user_dir), &phrase)) {
    combined += phrase;
    any = true;
  }
  if (ReadAll(Join(user_dir, kUserTerms), &terms)) {
    combined += "\n--autoime-user-terms--\n";
    combined += terms;
    any = true;
  }
  if (!any) return std::string();
  return PhraseVersionOf(combined);
}

// ---- phrase transaction journal ------------------------------------------

void DeleteJournal(const std::string& user_dir) {
  DeleteFileW(Wide(Join(user_dir, kTxnJournal)).c_str());
}

// Restores the pre-transaction phrase file and drops the compiled output so
// the next engine start rebuilds from the restored source.
void RestorePhrase(const std::string& user_dir, bool had_previous) {
  const std::string backup = Join(user_dir, kTxnBackup);
  if (had_previous && FileExists(backup)) {
    CopyFileW(Wide(backup).c_str(), Wide(PhrasePath(user_dir)).c_str(), FALSE);
  } else {
    DeleteFileW(Wide(PhrasePath(user_dir)).c_str());
  }
  DeleteFileW(Wide(PhraseMarkerPath(user_dir)).c_str());
  RemoveTree(Join(user_dir, kBuildDir));
  DeleteFileW(Wide(backup).c_str());
  DeleteJournal(user_dir);
}

// A previous process died between publish and commit: put the old phrase file
// back (or remove the half-published one) and force a rebuild.
bool RecoverPending(const std::string& user_dir, std::string* detail) {
  const std::string journal = Join(user_dir, kTxnJournal);
  std::string body;
  if (!ReadAll(journal, &body)) return false;
  bool had_previous = body.find("had_previous=1") != std::string::npos;
  RestorePhrase(user_dir, had_previous);
  if (detail)
    *detail = had_previous
                  ? "recovered an interrupted phrase transaction (previous "
                    "version restored)"
                  : "recovered an interrupted phrase transaction (no previous "
                    "version)";
  diag::Event("phrase_txn", "recovered");
  return true;
}

bool WriteJournal(const std::string& user_dir, bool had_previous) {
  std::string body = "phase=published\n";
  body += had_previous ? "had_previous=1\n" : "had_previous=0\n";
  return WriteAll(Join(user_dir, kTxnJournal), body);
}

bool SourceMatches(const PhraseRequest& req, std::string* detail) {
  std::string current_bytes;
  const bool exists = ReadAll(PhrasePath(req.user_dir), &current_bytes);
  if (req.source_missing) {
    if (exists) {
      if (detail) *detail = "phrase file appeared after it was read";
      return false;
    }
    return true;
  }
  if (!exists) {
    if (detail) *detail = "phrase file disappeared after it was read";
    return false;
  }
  std::string expected;
  if (!ReadAll(req.source_file, &expected)) {
    if (detail) *detail = "source snapshot unreadable";
    return false;
  }
  if (expected != current_bytes) {
    if (detail) *detail = "phrase file changed after it was read";
    return false;
  }
  return true;
}

std::string Trim(const std::string& s) {
  size_t a = 0;
  size_t b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' ||
                   s[b - 1] == '\n')) --b;
  return s.substr(a, b - a);
}

}  // namespace

std::string PhraseVersionOf(const std::string& bytes) {
  // ACE AV byte-domain hash (python parts phrase-management.md): the manager
  // and this tool only need a stable, order-sensitive identity for reporting
  // and journaling; the source check itself compares bytes.
  long long h = 1469598103LL;
  for (unsigned char b : bytes) {
    h = (h * 1000003LL + static_cast<long long>(b)) % 2147483647LL;
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%lld", h);
  return buffer;
}

bool RemoveTree(const std::string& path) {
  if (!DirExists(path)) {
    return DeleteFileW(Wide(path).c_str()) != 0 || !FileExists(path);
  }
  WIN32_FIND_DATAW fd{};
  HANDLE find = FindFirstFileW(Wide(Join(path, "*")).c_str(), &fd);
  if (find != INVALID_HANDLE_VALUE) {
    do {
      const std::string name = Utf8(fd.cFileName);
      if (name == "." || name == "..") continue;
      const std::string full = Join(path, name);
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        RemoveTree(full);
      } else {
        SetFileAttributesW(Wide(full).c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(Wide(full).c_str());
      }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
  }
  return RemoveDirectoryW(Wide(path).c_str()) != 0;
}

DeployLock::DeployLock(unsigned timeout_ms) {
  const char* names[] = {"Local\\AutoIME.Rime.Deploy",
                         "Global\\AutoIME.Rime.Deploy"};
  for (const char* name : names) {
    handle_ = CreateMutexW(nullptr, FALSE, Wide(name).c_str());
    if (!handle_) continue;
    const DWORD wait = WaitForSingleObject(handle_, timeout_ms);
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
      acquired_ = true;
      return;
    }
    if (wait == WAIT_TIMEOUT) busy_ = true;
    CloseHandle(handle_);
    handle_ = nullptr;
    if (busy_) return;  // a busy gate is not helped by the second namespace
  }
}

DeployLock::~DeployLock() {
  if (handle_) {
    if (acquired_) ReleaseMutex(handle_);
    CloseHandle(handle_);
    handle_ = nullptr;
  }
}

std::string ResolveResourcesDir(const std::string& module_dir) {
  const std::string env = Env("AUTOIME_RESOURCES_DIR");
  if (!env.empty() && DirExists(env)) return env;
  // PLAN-071 T-01: the shipped resources live in <autoime>\shared\resources;
  // the third suffix reaches it from the dev build tree (windows/ime/build).
  const char* const suffixes[] = {"\\resources", "\\..\\resources",
                                  "\\..\\..\\resources",
                                  "\\..\\..\\..\\shared\\resources"};
  for (const char* suffix : suffixes) {
    const std::string candidate = module_dir + suffix;
    if (DirExists(candidate)) return candidate;
  }
  return std::string();
}

std::string DefaultUserDir() {
  const std::string env = Env("AUTOIME_RIME_USER_DATA_DIR");
  if (!env.empty()) return env;
  return Join(Env("LOCALAPPDATA"), "AutoIME\\rime");
}

Manifest BuildManifest(const std::string& resources_dir, std::string* error) {
  Manifest manifest;
  std::string combined;
  for (const char* name : kManaged) {
    const std::string path = Join(resources_dir, name);
    std::string data;
    if (!ReadAll(path, &data)) {
      if (error) *error = std::string("missing resource: ") + name;
      return Manifest{};
    }
    FileEntry entry;
    entry.name = name;
    entry.size = data.size();
    entry.hash = Fnv1a(data.data(), data.size());
    manifest.files.push_back(entry);
    combined += name;
    combined += std::to_string(entry.size);
    combined += std::to_string(entry.hash);
  }
  manifest.digest = Fnv1a(combined.data(), combined.size());
  return manifest;
}

Status Prepare(const std::string& resources_dir, const std::string& user_dir) {
  Status status;
  status.resources_dir = resources_dir;
  status.user_dir = user_dir;
  DeployLock lock(30000);
  if (!lock.acquired()) {
    status.detail = lock.busy() ? "deployment busy" : "deployment lock unavailable";
    return status;
  }
  std::string error;
  const Manifest manifest = BuildManifest(resources_dir, &error);
  if (manifest.empty()) {
    status.detail = error.empty() ? "empty resources" : error;
    diag::Event("deploy", "resources_missing");
    return status;
  }
  status.version = VersionString(manifest.digest);
  if (!EnsureDir(user_dir)) {
    status.detail = "cannot create user data dir";
    return status;
  }

  // Fast path requires BOTH the managed resource identity and the phrase file
  // identity to match: a phrase edit leaves the resource version untouched and
  // must still recompiled the phrase table (V-04).
  const std::string phrase_version = CurrentPhraseVersion(user_dir);
  const std::string marker = MarkerPath(user_dir);
  std::string current;
  std::string phrase_marker;
  const bool phrase_marker_ok =
      ReadAll(PhraseMarkerPath(user_dir), &phrase_marker) &&
      Trim(phrase_marker) == phrase_version;
  if (ReadAll(marker, &current) && Trim(current) == status.version &&
      phrase_marker_ok) {
    status.ok = true;
    status.unchanged = true;
    diag::Event("deploy", "unchanged");
    return status;
  }

  // Back up the current set before replacing anything.
  const std::string backup = Join(user_dir, kBackupDir);
  RemoveTree(backup);
  bool had_previous = false;
  for (const char* name : kManaged) {
    const std::string src = Join(user_dir, name);
    if (!FileExists(src)) continue;
    EnsureDir(backup);
    if (CopyFileW(Wide(src).c_str(), Wide(Join(backup, name)).c_str(), FALSE)) {
      had_previous = true;
    }
  }
  if (had_previous && FileExists(marker))
    CopyFileW(Wide(marker).c_str(), Wide(Join(backup, kMarker)).c_str(), FALSE);
  if (FileExists(PhraseMarkerPath(user_dir)))
    CopyFileW(Wide(PhraseMarkerPath(user_dir)).c_str(),
              Wide(Join(backup, kPhraseMarker)).c_str(), FALSE);

  // Stage the new files, verify their hashes, then move them into place.
  const std::string stage = Join(user_dir, kStageDir);
  RemoveTree(stage);
  EnsureDir(stage);
  bool copied_all = true;
  for (const FileEntry& entry : manifest.files) {
    const std::string src = Join(resources_dir, entry.name);
    const std::string staged = Join(stage, entry.name);
    if (!CopyFileW(Wide(src).c_str(), Wide(staged).c_str(), FALSE)) {
      status.detail = "copy failed: " + entry.name;
      copied_all = false;
      break;
    }
    std::string data;
    if (!ReadAll(staged, &data) || data.size() != entry.size ||
        Fnv1a(data.data(), data.size()) != entry.hash) {
      status.detail = "hash mismatch after copy: " + entry.name;
      copied_all = false;
      break;
    }
  }
  if (copied_all) {
    for (const FileEntry& entry : manifest.files) {
      if (!MoveFileExW(Wide(Join(stage, entry.name)).c_str(),
                       Wide(Join(user_dir, entry.name)).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        status.detail = "install failed: " + entry.name;
        copied_all = false;
        break;
      }
    }
  }
  RemoveTree(stage);
  if (!copied_all) {
    // Leave the marker untouched so the previous version stays authoritative.
    diag::Event("deploy", "prepare_failed");
    return status;
  }
  // The compiled output belongs to the previous source set; drop it so the
  // next initialization recompiles. Otherwise a stale build directory can mask
  // a broken schema file and defeat the last-known-good rollback.
  RemoveTree(Join(user_dir, kBuildDir));
  if (!FileExists(Join(user_dir, kPhrase))) {
    CopyFileW(Wide(Join(resources_dir, kPhrase)).c_str(),
              Wide(Join(user_dir, kPhrase)).c_str(), FALSE);
  }
  if (!FileExists(Join(user_dir, kUserTerms))) {
    CopyFileW(Wide(Join(resources_dir, kUserTerms)).c_str(),
              Wide(Join(user_dir, kUserTerms)).c_str(), FALSE);
  }
  // The pending marker carries the phrase identity this prepare observed;
  // Commit promotes it only after the engine validated the new set.
  std::string pending = status.version + "\n";
  pending += CurrentPhraseVersion(user_dir);
  pending += "\n";
  if (!WriteAll(Join(user_dir, kPending), pending)) {
    status.detail = "cannot write pending marker";
    diag::Event("deploy", "marker_failed");
    return status;
  }
  status.ok = true;
  status.copied = true;
  diag::Event("deploy", "prepared");
  return status;
}

void Commit(const std::string& user_dir) {
  DeployLock lock(30000);
  const std::string pending = Join(user_dir, kPending);
  std::string body;
  if (FileExists(pending) && ReadAll(pending, &body)) {
    // Pending layout: "<resources version>\n<phrase version>\n"
    const size_t first = body.find('\n');
    if (first != std::string::npos) {
      const std::string resources = Trim(body.substr(0, first));
      const std::string phrase = Trim(body.substr(first + 1));
      WriteAll(MarkerPath(user_dir), resources + "\n");
      WriteAll(PhraseMarkerPath(user_dir), phrase + "\n");
    } else {
      WriteAll(MarkerPath(user_dir), Trim(body) + "\n");
    }
    DeleteFileW(Wide(pending).c_str());
  }
  RemoveTree(Join(user_dir, kBackupDir));
}

bool Rollback(const std::string& user_dir, std::string* detail) {
  DeployLock lock(30000);
  const std::string backup = Join(user_dir, kBackupDir);
  const std::string marker = MarkerPath(user_dir);
  bool restored = false;
  if (DirExists(backup) && FileExists(Join(backup, kMarker))) {
    for (const char* name : kManaged) {
      const std::string src = Join(backup, name);
      if (FileExists(src))
        CopyFileW(Wide(src).c_str(), Wide(Join(user_dir, name)).c_str(), FALSE);
    }
    CopyFileW(Wide(Join(backup, kMarker)).c_str(), Wide(marker).c_str(), FALSE);
    if (FileExists(Join(backup, kPhraseMarker))) {
      CopyFileW(Wide(Join(backup, kPhraseMarker)).c_str(),
                Wide(PhraseMarkerPath(user_dir)).c_str(), FALSE);
    }
    restored = true;
  } else {
    for (const char* name : kManaged)
      DeleteFileW(Wide(Join(user_dir, name)).c_str());
    DeleteFileW(Wide(marker).c_str());
    DeleteFileW(Wide(PhraseMarkerPath(user_dir)).c_str());
  }
  DeleteFileW(Wide(Join(user_dir, kPending)).c_str());
  RemoveTree(backup);
  RemoveTree(Join(user_dir, kStageDir));
  // Drop only AutoIME's own compile output so the retry rebuilds from the
  // restored sources; the Luna/stroke artifacts and shared dictionary tables
  // stay valid and are reused (recovery time budget).
  const std::string build = Join(user_dir, kBuildDir);
  const char* const stale[] = {
      "autoime_pinyin.schema.yaml", "autoime_pinyin.prism.bin",
      "autoime_pinyin.table.bin",   "autoime_terms.table.bin",
      "autoime_terms.prism.bin",    "autoime_terms.reverse.bin",
      "autoime_en.table.bin",       "autoime_en.reverse.bin",
  };
  for (const char* name : stale) DeleteFileW(Wide(Join(build, name)).c_str());
  if (detail)
    *detail = restored ? "restored last known-good resources"
                       : "no previous resources; primary schema disabled";
  diag::Event("deploy", restored ? "rolled_back" : "rolled_back_empty");
  return restored;
}

// Public recovery entry: any next deployment (phrase transaction or the plain
// maintenance command) settles a transaction a previous process left pending.
bool RecoverPendingPhrase(const std::string& user_dir, std::string* detail) {
  DeployLock lock(30000);
  if (!lock.acquired()) return false;
  return RecoverPending(user_dir, detail);
}

bool ReadPhraseRequest(const std::string& request_path, PhraseRequest* out,
                       std::string* error) {
  std::string body;
  if (!ReadAll(request_path, &body)) {
    if (error) *error = "request file unreadable";
    return false;
  }
  std::string source_kind;
  size_t pos = 0;
  while (pos <= body.size()) {
    const size_t nl = body.find('\n', pos);
    const std::string line =
        Trim(body.substr(pos, nl == std::string::npos ? std::string::npos
                                                     : nl - pos));
    if (!line.empty() && line[0] != '#') {
      const size_t eq = line.find('=');
      if (eq == std::string::npos) {
        if (error) *error = "malformed request line";
        return false;
      }
      const std::string key = Trim(line.substr(0, eq));
      const std::string value = line.substr(eq + 1);
      if (key == "user") out->user_dir = value;
      else if (key == "source") out->source_file = value;
      else if (key == "source_missing") out->source_missing = value == "1";
      else if (key == "staged") out->staged_file = value;
      else if (key == "verify") out->verify_file = value;
      else if (key == "timeout_ms") out->lock_timeout_ms = static_cast<unsigned>(std::atoi(value.c_str()));
    }
    if (nl == std::string::npos) break;
    pos = nl + 1;
  }
  if (out->user_dir.empty() || out->staged_file.empty()) {
    if (error) *error = "request missing user/staged";
    return false;
  }
  if (out->lock_timeout_ms > 30000) out->lock_timeout_ms = 30000;
  return true;
}

PhraseResult ApplyPhraseFile(const std::string& request_path,
                             const std::function<bool(std::string*)>& maintain) {
  PhraseResult result;
  PhraseRequest req;
  std::string error;
  if (!ReadPhraseRequest(request_path, &req, &error)) {
    result.code = 4;
    result.detail = error;
    return result;
  }
  if (!EnsureDir(req.user_dir)) {
    result.code = 4;
    result.detail = "cannot create user data dir";
    return result;
  }
  if (!FileExists(req.staged_file)) {
    result.code = 4;
    result.detail = "staged phrase file missing";
    return result;
  }
  std::string staged;
  if (!ReadAll(req.staged_file, &staged)) {
    result.code = 4;
    result.detail = "staged phrase file unreadable";
    return result;
  }
  result.after_version = PhraseVersionOf(staged);

  DeployLock lock(req.lock_timeout_ms);
  if (!lock.acquired()) {
    if (lock.busy()) {
      result.code = 7;
      result.busy = true;
      result.detail = "deployment busy; retry";
      return result;
    }
    result.code = 4;
    result.detail = "deployment lock unavailable";
    return result;
  }

  // Recover a crashed predecessor first; its old version is authoritative.
  std::string recovered_detail;
  if (RecoverPending(req.user_dir, &recovered_detail)) {
    result.recovered = true;
    result.detail = recovered_detail;
  } else if (FileExists(Join(req.user_dir, kTxnBackup))) {
    // R-07 repair: a backup without a journal can only be debris from a
    // process that died before journaling; it must not leak into later runs.
    DeleteFileW(Wide(Join(req.user_dir, kTxnBackup)).c_str());
    diag::Event("phrase_txn", "orphan_backup_cleaned");
  }

  std::string current_bytes;
  const bool exists = ReadAll(PhrasePath(req.user_dir), &current_bytes);
  result.before_version = exists ? PhraseVersionOf(current_bytes) : std::string();
  if (!SourceMatches(req, &result.detail)) {
    result.code = 5;
    result.conflict = true;
    result.detail += "; re-read the phrase file and retry";
    diag::Event("phrase_txn", "conflict");
    return result;
  }

  const std::string backup = Join(req.user_dir, kTxnBackup);
  if (exists) {
    if (!CopyFileW(Wide(PhrasePath(req.user_dir)).c_str(), Wide(backup).c_str(),
                   FALSE)) {
      result.code = 4;
      result.detail = "cannot back up phrase file";
      return result;
    }
  } else {
    DeleteFileW(Wide(backup).c_str());
  }
  if (!WriteJournal(req.user_dir, exists)) {
    DeleteFileW(Wide(backup).c_str());
    result.code = 4;
    result.detail = "cannot write transaction journal";
    return result;
  }
  // R-07 repair: the seam sits AFTER the journal so the covered state is
  // "journal written, not yet published" (a later recovery must restore the
  // previous file and clear journal+backup).
  if (Env("AUTOIME_TEST_CRASH_AFTER_JOURNAL") == "1") {
    std::_Exit(98);
  }
  // From here on any failure restores the previous phrase file.
  if (!MoveFileExW(Wide(req.staged_file).c_str(),
                   Wide(PhrasePath(req.user_dir)).c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteJournal(req.user_dir);
    DeleteFileW(Wide(backup).c_str());
    result.code = 4;
    result.detail = "cannot publish staged phrase file";
    return result;
  }
  diag::Event("phrase_txn", "published");

  // Test-only crash seams (PLAN-072 V-06): simulate a process death at the two
  // recovery-relevant points. Never set by the product.
  if (Env("AUTOIME_TEST_CRASH_AFTER_PUBLISH") == "1") {
    std::_Exit(99);
  }

  std::string maintain_error;
  if (!maintain(&maintain_error)) {
    RestorePhrase(req.user_dir, exists);
    result.code = 6;
    result.rolled_back = true;
    result.detail = maintain_error.empty() ? "deployment failed"
                                           : maintain_error;
    diag::Event("phrase_txn", "rolled_back");
    return result;
  }

  // The maintenance step validated the engine with the new phrase set (and
  // wrote the phrase marker through Commit); settle the transaction.
  DeleteJournal(req.user_dir);
  DeleteFileW(Wide(backup).c_str());
  DeleteFileW(Wide(req.verify_file).c_str());
  result.code = 0;
  if (result.detail.empty()) result.detail = "applied";
  diag::Event("phrase_txn", "committed");
  return result;
}

bool CleanupLegacyLog(std::string* path) {
  wchar_t temp[MAX_PATH]{};
  const DWORD n = GetTempPathW(MAX_PATH, temp);
  if (n == 0) return false;
  const std::string legacy = Utf8(std::wstring(temp, n)) + "autoime-ime.log";
  if (path) *path = legacy;
  if (!FileExists(legacy)) return false;
  return DeleteFileW(Wide(legacy).c_str()) != 0;
}

}  // namespace deployment
