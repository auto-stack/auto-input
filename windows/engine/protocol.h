// AutoIME shared-engine IPC protocol (PLAN-078 T-02 prototype).
//
// One per-user engine process owns librime and the writable user dictionary;
// TSF clients (and the manager/deploy tools) talk to it over a local named
// pipe. Messages are length-prefixed frames in a single pipe message each:
//
//   [Header][payload]
//
// Every request carries the engine session it targets (0 when it applies to
// the connection instead). Strings are length-prefixed UTF-8; UTF-16 text is
// length-prefixed UTF-16LE code units. The protocol is versioned in the
// frame header: the owner rejects a client whose header version differs, and
// the client keeps retrying or starts its own owner after an upgrade, so new
// and old semantics are never mixed.
#ifndef AUTOIME_ENGINE_PROTOCOL_H_
#define AUTOIME_ENGINE_PROTOCOL_H_

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace autoime {
namespace ipc {

constexpr uint32_t kMagic = 0x41494D45;  // 'AIME'
constexpr uint16_t kProtocolVersion = 1;
constexpr size_t kMaxPayload = 4u << 20;  // 4 MiB, one whole snapshot
constexpr wchar_t kPipePrefix[] = L"\\\\.\\pipe\\autoime-engine-";
constexpr wchar_t kMutexPrefix[] = L"Local\\AutoIME.Engine.";

enum class Op : uint16_t {
  kHello = 1,        // client: {u32 pid}; version lives in the header
  kHelloReply,       // server: {i32 ok, str engine_version, str resources}
  kCreateSession,    // client: {}
  kCreateSessionReply,  // server: {i32 ok, u64 session}
  kDestroySession,   // client: {}
  kKey,              // client: {i32 keysym, i32 mods}; server: {i32 consumed}
  kContext,          // client: {}; server: snapshot
  kStatus,           // server: {i32 ascii, i32 disabled}
  kSetOption,        // client: {str name, i32 value}
  kSelect,           // client: {i32 page_index}; server: {i32 ok}
  kCommit,           // server: {i32 has_commit, str text_utf8}
  kClear,            // client: {}
  kPing,             // server: {u64 server_tick_ms}
  kShutdown,         // client: {}; server acknowledges then exits
  kBye,              // client: connection closes
  // PLAN-078 T-02 confirmation contract: the engine learns when a candidate is
  // confirmed; the client reports whether the host applied the commit text.
  // A failed host edit is compensated with kForget so the learned entry is
  // removed again (V-03 "宿主提交失败不计入").
  kConfirm,          // client: {}; server: {i32 ok}
  kForget,           // client: {str code, str text}; server: {i32 ok}
  // Owner-side redeploy (install/upgrade and manager transactions): a second
  // process must not replace files the owner has mapped.
  kDeploy,           // client: {str resources_dir, str user_data_dir}
                     // server: {i32 ok, i32 fallback, i32 disabled, str schema,
                     //          str version, str detail, str error}
};

#pragma pack(push, 1)
struct Header {
  uint32_t magic;
  uint16_t version;
  uint16_t op;
  uint32_t payload_len;
  uint32_t reserved;
  uint64_t session;
};
#pragma pack(pop)

static_assert(sizeof(Header) == 24, "unexpected IPC header padding");

// Snapshot wire flags.
constexpr uint8_t kSnapComposing = 1u << 0;
constexpr uint8_t kSnapLastPage = 1u << 1;
// The owner still knows this session. False after an owner-side redeploy
// finalized every session; the client must recreate its session.
constexpr uint8_t kSnapSessionOk = 1u << 2;

class Writer {
 public:
  void U8(uint8_t v) { data_.push_back(static_cast<char>(v)); }
  void I32(int32_t v) { Raw(&v, sizeof(v)); }
  void U32(uint32_t v) { Raw(&v, sizeof(v)); }
  void U64(uint64_t v) { Raw(&v, sizeof(v)); }
  void Bytes(const void* p, size_t n) { Raw(p, n); }
  void Str(const std::string& s) {
    U32(static_cast<uint32_t>(s.size()));
    if (!s.empty()) Raw(s.data(), s.size());
  }
  void WStr(const std::wstring& s) {
    U32(static_cast<uint32_t>(s.size()));
    if (!s.empty()) Raw(s.data(), s.size() * sizeof(wchar_t));
  }
  const std::string& data() const { return data_; }
  std::string take() { return std::move(data_); }

 private:
  void Raw(const void* p, size_t n) {
    data_.append(static_cast<const char*>(p), n);
  }
  std::string data_;
};

class Reader {
 public:
  Reader(const char* data, size_t size) : data_(data), size_(size) {}
  bool U8(uint8_t* v) { return Take(v, sizeof(*v)); }
  bool I32(int32_t* v) { return Take(v, sizeof(*v)); }
  bool U32(uint32_t* v) { return Take(v, sizeof(*v)); }
  bool U64(uint64_t* v) { return Take(v, sizeof(*v)); }
  bool Str(std::string* s) {
    uint32_t n = 0;
    if (!U32(&n) || !Room(n)) return false;
    s->assign(data_ + pos_, n);
    pos_ += n;
    return true;
  }
  bool WStr(std::wstring* s) {
    uint32_t n = 0;
    if (!U32(&n) || !Room(static_cast<size_t>(n) * sizeof(wchar_t))) return false;
    s->assign(reinterpret_cast<const wchar_t*>(data_ + pos_), n);
    pos_ += static_cast<size_t>(n) * sizeof(wchar_t);
    return true;
  }
  size_t remaining() const { return size_ - pos_; }

 private:
  template <typename T>
  bool Take(T* out, size_t n) {
    if (!Room(n)) return false;
    std::memcpy(out, data_ + pos_, n);
    pos_ += n;
    return true;
  }
  bool Room(size_t n) const { return pos_ + n <= size_; }

  const char* data_;
  size_t size_;
  size_t pos_ = 0;
};

}  // namespace ipc
}  // namespace autoime

#endif  // AUTOIME_ENGINE_PROTOCOL_H_
