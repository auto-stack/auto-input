// AutoIME - resource deployment, recovery, and phrase transactions
// (PLAN-070 T-04; PLAN-072 T-03).
//
// The shipped Rime resources live in one directory next to the installed DLL
// (<install>\resources). Deployment copies them into AutoIME's private user
// data dir, serializes concurrent cold starts with a named mutex, keeps a
// backup of the last known-good set, and rolls back when the new set fails to
// compile so the engine can fall back to the last working resources (and then
// to the Luna schema, and finally to keyboard pass-through).
//
// PLAN-072 T-03 adds the phrase transaction: the manager stages a complete
// custom_phrase.txt plus the exact source bytes it read; this module re-checks
// the source under the deployment lock, publishes the staged file, and keeps a
// journal + backup so an interrupted transaction is recovered on the next run.
// All path handling is UTF-8 at the API boundary and UTF-16 at Win32 (Chinese
// user names and install paths are first-class).
#ifndef AUTOIME_DEPLOYMENT_H_
#define AUTOIME_DEPLOYMENT_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace deployment {

struct FileEntry {
  std::string name;
  uint64_t size = 0;
  uint64_t hash = 0;  // FNV-1a 64 over the file bytes
};

struct Manifest {
  std::vector<FileEntry> files;
  uint64_t digest = 0;  // combined content hash = resource version identity
  bool empty() const { return files.empty(); }
};

struct Status {
  bool ok = false;
  bool copied = false;
  bool unchanged = false;
  bool restored_backup = false;
  std::string resources_dir;
  std::string user_dir;
  std::string version;  // "r<hex digest>"
  std::string detail;
};

// Cross-process deployment gate: two hosts cold-starting at the same time
// serialize on this lock so only one compiles the dictionaries. The phrase
// transaction holds the same lock across publish + maintenance so a deploy and
// a phrase edit never interleave.
class DeployLock {
 public:
  // Waits up to `timeout_ms` (default: the historical 30 s).
  explicit DeployLock(unsigned timeout_ms = 30000);
  ~DeployLock();
  DeployLock(const DeployLock&) = delete;
  DeployLock& operator=(const DeployLock&) = delete;
  bool acquired() const { return acquired_; }
  bool busy() const { return busy_; }

 private:
  void* handle_ = nullptr;
  bool acquired_ = false;
  bool busy_ = false;
};

// Resolution order: AUTOIME_RESOURCES_DIR -> <module_dir>\resources ->
// <module_dir>\..\resources -> <module_dir>\..\..\resources (dev build tree).
std::string ResolveResourcesDir(const std::string& module_dir);

// %LOCALAPPDATA%\AutoIME\rime (AUTOIME_RIME_USER_DATA_DIR overrides).
std::string DefaultUserDir();

// Builds the manifest of a resources directory (does not touch the user dir).
Manifest BuildManifest(const std::string& resources_dir, std::string* error);

// Copies/updates the managed resource files into the user dir. The previous
// set is backed up first; custom_phrase.txt is never overwritten. The
// unchanged fast path covers BOTH the managed resource digest and the phrase
// file content, so editing a phrase and redeploying can never be mistaken for
// "no change" (PLAN-072 T-03, V-04).
Status Prepare(const std::string& resources_dir, const std::string& user_dir);

// Marks the prepared resources as last known-good and drops the backup.
void Commit(const std::string& user_dir);

// Restores the backed-up resources (or removes the failed set), drops the
// staged compile output and returns whether a previous set was restored.
bool Rollback(const std::string& user_dir, std::string* detail);

// Identity of a phrase file's bytes: the ACE AV byte-domain hash
// (h = (h * 1000003 + byte) mod 2^31-1), as a decimal string. Used only for
// reporting and journal identity; the source check itself is byte-exact.
std::string PhraseVersionOf(const std::string& bytes);

// One phrase transaction request, written by the Auto manager as a UTF-8
// key/value file and passed with `--request <path>`.
struct PhraseRequest {
  std::string user_dir;
  std::string source_file;   // exact bytes the manager parsed
  bool source_missing = false;  // the manager read a missing data file
  std::string staged_file;   // complete new phrase file
  std::string verify_file;   // optional: present/absent candidate checks
  unsigned lock_timeout_ms = 2000;
};

struct PhraseResult {
  // 0 ok, 4 io/precondition, 5 conflict, 6 deploy/verification failure
  // (previous version restored), 7 busy (lock not acquired; retry).
  int code = 0;
  bool conflict = false;
  bool busy = false;
  bool rolled_back = false;
  bool recovered = false;
  std::string detail;  // no phrase content, no keys
  std::string before_version;
  std::string after_version;
};

// Verifies a UTF-8 key/value request file and executes the phrase transaction.
// `maintain` runs the Rime maintenance step while the lock is held; returning
// false rolls the phrase file back and reports code 6.
PhraseResult ApplyPhraseFile(
    const std::string& request_path,
    const std::function<bool(std::string*)>& maintain);

// Settles a transaction a previous process left pending (crash recovery).
bool RecoverPendingPhrase(const std::string& user_dir, std::string* detail);

// Parses a `--request` file into `out` (UTF-8, `key=value` lines).
bool ReadPhraseRequest(const std::string& request_path, PhraseRequest* out,
                       std::string* error);

// Removes %TEMP%\autoime-ime.log (the PLAN-069 prototype wrote plaintext key
// traces there). Returns true when a file was found and deleted.
bool CleanupLegacyLog(std::string* path);

// Recursively deletes a directory tree (best effort).
bool RemoveTree(const std::string& path);

}  // namespace deployment

#endif  // AUTOIME_DEPLOYMENT_H_
