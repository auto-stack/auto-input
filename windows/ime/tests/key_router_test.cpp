// AutoIME automated fixture (PLAN-070 T-01).
//
// Replays the TSF callback shapes that PLAN-069 left unverified, against the
// real KeyRouter used in production:
//   * Test -> Test -> Down        (repeated Test callback)
//   * Down only                   (hosts that skip Test)
//   * Test -> Down ... repeated   (keyboard auto-repeat)
//   * Test, focus change, Down    (late callback from the previous focus)
// plus modifier ownership (Ctrl/Alt/Win/AltGr), Shift-click mode switching,
// Shift+letter case, sensitive scopes, idle vs composing keys and the
// payload-free diagnostics contract.
//
// Exit code 0 = all cases pass, 5 = at least one case failed.
#include "../candidate_layout.h"
#include "../candidate_window.h"
#include "../diag.h"
#include "../edit_model.h"
#include "../key_router.h"
#include "../rime_engine.h"

#include <windows.h>

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// The engine adapter calls ImeLog for structural diagnostics (the DLL owns the
// real implementation). The fixture writes them to stderr only, mirroring
// autoime_deploy: payload-free by contract.
void ImeLog(const char* fmt, ...) {
  if (!std::getenv("AUTOIME_FIXTURE_LOG")) return;
  va_list args;
  va_start(args, fmt);
  std::fprintf(stderr, "[engine] ");
  std::vfprintf(stderr, fmt, args);
  std::fprintf(stderr, "\n");
  va_end(args);
}

// The production DLL owns g_hInst (dllmain.cpp). The fixture links the same
// CandidateWindow code for the PLAN-078 T-06 window oracle, so it supplies the
// module handle itself.
HINSTANCE g_hInst = nullptr;

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    ++g_checks;                                                            \
    if (!(cond)) {                                                         \
      ++g_failures;                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                      \
  } while (0)

struct FakeTarget final : IKeyTarget {
  bool composing = false;
  bool ascii = false;
  bool sensitive = false;
  bool ready = true;
  bool accept = true;
  int ops = 0;
  int toggles = 0;
  int cancels = 0;
  int last_keysym = -1;
  int last_mask = -1;

  bool Composing() override { return composing; }
  bool AsciiMode() override { return ascii; }
  bool EngineReady() override { return ready; }
  bool Sensitive() override { return sensitive; }
  bool ProcessKey(int keysym, int mask) override {
    ++ops;
    last_keysym = keysym;
    last_mask = mask;
    return accept;
  }
  void ToggleMode() override {
    ++toggles;
    ascii = !ascii;
  }
  void CancelComposition() override {
    ++cancels;
    composing = false;
  }
};

KeyEvent Make(WPARAM vk, bool shift = false, bool ctrl = false, bool alt = false,
              bool win = false, bool repeat = false) {
  KeyEvent key;
  key.vk = vk;
  key.mods.shift = shift;
  key.mods.ctrl = ctrl;
  key.mods.alt = alt;
  key.mods.win = win;
  key.repeat = repeat;
  return key;
}

void CaseRepeatedTestProcessesOnce() {
  FakeTarget t;
  KeyRouter r;
  r.SetTarget(&t);
  const KeyEvent n = Make('N');
  const RouteDecision first = r.OnTestKeyDown(n, 1);
  const RouteDecision second = r.OnTestKeyDown(n, 1);
  CHECK(first.eat && second.eat);
  CHECK(t.ops == 0);  // Test must never advance the engine
  const RouteDecision down = r.OnKeyDown(n, 1);
  CHECK(down.eat);
  CHECK(t.ops == 1);
  CHECK(t.last_keysym == 'n');
}

void CaseDownOnlyProcessesOnce() {
  FakeTarget t;
  KeyRouter r;
  r.SetTarget(&t);
  const RouteDecision d = r.OnKeyDown(Make('N'), 1);
  CHECK(d.eat && t.ops == 1 && t.last_keysym == 'n');
}

void CaseAutoRepeatProcessesEachPress() {
  FakeTarget t;
  KeyRouter r;
  r.SetTarget(&t);
  const KeyEvent first = Make('N');
  const KeyEvent repeat = Make('N', false, false, false, false, true);
  r.OnTestKeyDown(first, 1);
  r.OnKeyDown(first, 1);
  r.OnTestKeyDown(repeat, 1);
  r.OnKeyDown(repeat, 1);
  CHECK(t.ops == 2);
  // Down-only host auto-repeat.
  FakeTarget t2;
  KeyRouter r2;
  r2.SetTarget(&t2);
  r2.OnKeyDown(first, 1);
  r2.OnKeyDown(repeat, 1);
  CHECK(t2.ops == 2);
}

void CaseStaleFocusCallbackDropped() {
  FakeTarget t;
  KeyRouter r;
  r.SetTarget(&t);
  const KeyEvent n = Make('N');
  CHECK(r.OnTestKeyDown(n, 1).eat);
  r.OnFocus(2, true);
  const RouteDecision late = r.OnKeyDown(n, 1);  // still tagged with old focus
  CHECK(!late.eat);
  CHECK(t.ops == 0);
  const RouteDecision fresh = r.OnKeyDown(n, 2);
  CHECK(fresh.eat);
  CHECK(t.ops == 1);
  // A Test from the old generation is dropped too.
  CHECK(!r.OnTestKeyDown(n, 1).eat);
}

void CaseShortcutsStayWithHost() {
  FakeTarget t;
  KeyRouter r;
  r.SetTarget(&t);
  const KeyEvent ctrl_s = Make('S', false, true);
  const KeyEvent alt_tab = Make(VK_TAB, false, false, true);
  const KeyEvent win = Make('E', false, false, false, true);
  const KeyEvent altgr = Make('E', false, true, true);  // Ctrl+Alt = AltGr
  CHECK(!r.OnKeyDown(ctrl_s, 1).eat);
  CHECK(!r.OnKeyDown(alt_tab, 1).eat);
  CHECK(!r.OnKeyDown(win, 1).eat);
  CHECK(!r.OnKeyDown(altgr, 1).eat);
  CHECK(t.ops == 0);
}

void CaseShiftClickTogglesMode() {
  FakeTarget t;
  KeyRouter r;
  r.SetTarget(&t);
  r.OnKeyDown(Make(VK_SHIFT, true), 1);
  r.OnKeyUp(Make(VK_SHIFT, true), 1);
  CHECK(t.toggles == 1);
  CHECK(t.ascii);
  // In English mode Shift+letter is the host's own capital letter.
  const KeyEvent shift_letter = Make('A', true);
  r.OnKeyDown(Make(VK_SHIFT, true), 1);
  r.OnKeyDown(shift_letter, 1);
  r.OnKeyUp(Make(VK_SHIFT, true), 1);
  CHECK(t.toggles == 1);
  CHECK(t.ops == 0);
  // Back in Chinese mode Shift+letter keeps its case for the engine and does
  // not toggle the mode.
  t.ascii = false;
  r.OnKeyDown(Make(VK_SHIFT, true), 1);
  r.OnKeyDown(shift_letter, 1);
  r.OnKeyUp(Make(VK_SHIFT, true), 1);
  CHECK(t.toggles == 1);
  CHECK(t.ops == 1);
  CHECK(t.last_keysym == 'A');
  CHECK(t.last_mask == 1);
  // Ctrl+Shift is a chord, not a click.
  r.OnKeyDown(Make(VK_SHIFT, true, true), 1);
  r.OnKeyUp(Make(VK_SHIFT, true, true), 1);
  CHECK(t.toggles == 1);
}

void CaseShiftClickCancelsComposition() {
  FakeTarget t;
  t.composing = true;
  KeyRouter r;
  r.SetTarget(&t);
  r.OnKeyDown(Make(VK_SHIFT, true), 1);
  r.OnKeyUp(Make(VK_SHIFT, true), 1);
  CHECK(t.cancels == 1);
  CHECK(t.toggles == 1);
}

void CaseSensitiveAndUnreadyPassThrough() {
  FakeTarget t;
  KeyRouter r;
  r.SetTarget(&t);
  t.sensitive = true;
  CHECK(!r.OnKeyDown(Make('N'), 1).eat);
  CHECK(!r.OnTestKeyDown(Make('N'), 1).eat);
  CHECK(t.ops == 0);
  t.sensitive = false;
  t.ready = false;
  CHECK(!r.OnKeyDown(Make('N'), 1).eat);
  CHECK(t.ops == 0);
}

void CaseAsciiModeTypesDirectly() {
  FakeTarget t;
  KeyRouter r;
  r.SetTarget(&t);
  t.ascii = true;
  CHECK(!r.OnKeyDown(Make('N'), 1).eat);
  CHECK(t.ops == 0);
  t.ascii = false;
  CHECK(r.OnKeyDown(Make('N'), 1).eat);
  CHECK(t.ops == 1);
}

void CaseIdleKeysStayWithHost() {
  FakeTarget t;
  KeyRouter r;
  r.SetTarget(&t);
  const WPARAM idle[] = {VK_SPACE, '1', VK_ESCAPE, VK_RETURN, VK_BACK,
                         VK_LEFT,  VK_PRIOR, VK_NEXT, VK_TAB};
  for (WPARAM vk : idle) CHECK(!r.OnKeyDown(Make(vk), 1).eat);
  CHECK(!r.OnKeyDown(Make(VK_F1), 1).eat);
  CHECK(t.ops == 0);
}

void CaseComposingKeysAreConsumed() {
  FakeTarget t;
  t.composing = true;
  KeyRouter r;
  r.SetTarget(&t);
  const WPARAM composing[] = {VK_SPACE, '1',  VK_ESCAPE, VK_RETURN,
                              VK_BACK,  VK_LEFT, VK_PRIOR,  VK_NEXT,
                              VK_TAB,   VK_OEM_COMMA};
  for (WPARAM vk : composing) CHECK(r.OnKeyDown(Make(vk), 1).eat);
  CHECK(t.ops == 10);
}

void CasePunctuationFollowsMode() {
  FakeTarget t;
  KeyRouter r;
  r.SetTarget(&t);
  CHECK(r.OnKeyDown(Make(VK_OEM_COMMA), 1).eat);  // Chinese punctuation
  t.ascii = true;
  CHECK(!r.OnKeyDown(Make(VK_OEM_COMMA), 1).eat);  // English punctuation
}

void CaseGeneratedKeysymMapping() {
  CHECK(KeysymForVk('A', false) == 'a');
  CHECK(KeysymForVk('A', true) == 'A');
  CHECK(KeysymForVk(VK_SPACE, false) == 0x20);
  CHECK(KeysymForVk(VK_ESCAPE, false) == 0xff1b);
  CHECK(KeysymForVk(VK_F1, false) == 0);
  CHECK(RimeModifierMask(Make('A', true).mods) == 1);
  CHECK(RimeModifierMask(Make('A', false, true, true).mods) == 12);
}

// PLAN-073 T-07 follow-up: a host may consume the Test callback and never send
// the matching KeyDown. Shift+symbol must still mark Shift as used, otherwise
// the Shift release in such hosts is misread as a mode click.
void CaseShiftTestOnlyDoesNotToggleMode() {
  FakeTarget t;
  KeyRouter r;
  r.SetTarget(&t);
  r.OnKeyDown(Make(VK_SHIFT, true), 1);
  const RouteDecision test = r.OnTestKeyDown(Make(VK_OEM_1, true), 1);
  CHECK(test.eat);
  CHECK(t.ops == 0);  // Test must not advance the engine
  r.OnKeyUp(Make(VK_SHIFT, true), 1);
  CHECK(t.toggles == 0);
  CHECK(!t.ascii);
  // Control: a real bare Shift click still toggles the mode.
  r.OnKeyDown(Make(VK_SHIFT, true), 1);
  r.OnKeyUp(Make(VK_SHIFT, true), 1);
  CHECK(t.toggles == 1);
  // Down-only hosts keep the existing contract: Down marks Shift used too.
  t.toggles = 0;
  t.ascii = false;
  r.OnKeyDown(Make(VK_SHIFT, true), 1);
  r.OnKeyDown(Make(VK_OEM_PLUS, true), 1);
  r.OnKeyUp(Make(VK_SHIFT, true), 1);
  CHECK(t.toggles == 0);
}

// PLAN-073 T-07: the keysym must be the character the user sees, because
// Rime's punctuator maps characters (not the shift bit) to punctuation.
void CaseShiftSymbolMapping() {
  CHECK(KeysymForVk(VK_OEM_1, false) == ';');
  CHECK(KeysymForVk(VK_OEM_1, true) == ':');
  CHECK(KeysymForVk(VK_OEM_PLUS, false) == '=');
  CHECK(KeysymForVk(VK_OEM_PLUS, true) == '+');
  CHECK(KeysymForVk(VK_OEM_2, true) == '?');
  CHECK(KeysymForVk(VK_OEM_3, true) == '~');
  CHECK(KeysymForVk(VK_OEM_4, true) == '{');
  CHECK(KeysymForVk(VK_OEM_5, true) == '|');
  CHECK(KeysymForVk(VK_OEM_6, true) == '}');
  CHECK(KeysymForVk(VK_OEM_7, true) == '"');
  CHECK(KeysymForVk(VK_OEM_COMMA, true) == '<');
  CHECK(KeysymForVk(VK_OEM_PERIOD, true) == '>');
  CHECK(KeysymForVk(VK_OEM_MINUS, true) == '_');
  CHECK(KeysymForVk('1', false) == '1');
  CHECK(KeysymForVk('1', true) == '!');
  CHECK(KeysymForVk('9', true) == '(');
  CHECK(KeysymForVk('0', true) == ')');
}

// PLAN-073 T-07: shifted symbols take the punctuation path in Chinese idle
// mode and keep the shifted character while composing; digits stay with the
// host when unshifted, and English mode never intercepts either.
void CaseShiftSymbolRouting() {
  FakeTarget t;
  KeyRouter r;
  r.SetTarget(&t);
  CHECK(r.OnKeyDown(Make(VK_OEM_1, true), 1).eat);
  CHECK(t.last_keysym == ':' && t.last_mask == 1);
  CHECK(r.OnKeyDown(Make(VK_OEM_PLUS, true), 1).eat);
  CHECK(t.last_keysym == '+' && t.last_mask == 1);
  CHECK(r.OnKeyDown(Make('1', true), 1).eat);
  CHECK(t.last_keysym == '!' && t.last_mask == 1);
  CHECK(!r.OnKeyDown(Make('1'), 1).eat);
  CHECK(!r.OnKeyDown(Make('0'), 1).eat);
  t.ascii = true;
  CHECK(!r.OnKeyDown(Make(VK_OEM_1, true), 1).eat);
  CHECK(!r.OnKeyDown(Make('1', true), 1).eat);
  t.ascii = false;
  t.composing = true;
  CHECK(r.OnKeyDown(Make(VK_OEM_1, true), 1).eat);
  CHECK(t.last_keysym == ':');
  CHECK(r.OnKeyDown(Make('3', true), 1).eat);
  CHECK(t.last_keysym == '#');
}

// The diagnostics log produced while driving a full synthetic key session
// must contain no key codes, pre-edit, selection or committed text.
void CaseDiagnosticsArePayloadFree() {
  char temp[MAX_PATH]{};
  GetTempPathA(MAX_PATH, temp);
  char path[MAX_PATH]{};
  std::snprintf(path, sizeof(path), "%sautoime-fixture-%lu.log", temp,
                static_cast<unsigned long>(GetCurrentProcessId()));
  DeleteFileA(path);
  diag::Init(path);
  {
    FakeTarget t;
    KeyRouter r;
    r.SetTarget(&t);
    const char* payloads[] = {"nihao", "mima", "你好", "密码", "payload"};
    for (const char* p : payloads) {
      KeyEvent key = Make(static_cast<WPARAM>(
          toupper(static_cast<unsigned char>(p[0]))));
      r.OnTestKeyDown(key, 1);
      r.OnKeyDown(key, 1);
    }
    r.OnKeyDown(Make(VK_SHIFT, true), 1);
    r.OnKeyUp(Make(VK_SHIFT, true), 1);
    r.OnKeyDown(Make(VK_ESCAPE), 1);
    r.OnKeyDown(Make('1'), 1);
    r.OnFocus(2, false);
    r.OnKeyDown(Make('N'), 1);
  }
  FILE* f = std::fopen(path, "rb");
  CHECK(f != nullptr);
  std::string content;
  if (f) {
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) content.append(buf, n);
    std::fclose(f);
  }
  for (const char* payload : {"nihao", "mima", "\xE4\xBD\xA0\xE5\xA5\xBD",
                              "\xE5\xAF\x86\xE7\xA0\x81", "payload", "vk="})
    CHECK(content.find(payload) == std::string::npos);
  CHECK(content.find("stage=key") != std::string::npos);
  // Closed vocabulary: every emitted code token is one of the known ones.
  const char* allowed[] = {"test_eat",      "test_pass", "op",
                           "op_declined",   "pass",      "stale_dropped",
                           "repeat",        "latency_us", "shift_click_toggle",
                           "in",            "out",       "reset"};
  size_t pos = 0;
  int lines = 0;
  while (pos < content.size()) {
    const size_t end = content.find('\n', pos);
    const std::string line = content.substr(
        pos, end == std::string::npos ? std::string::npos : end - pos);
    pos = end == std::string::npos ? content.size() : end + 1;
    ++lines;
    CHECK(line.size() < 200);
    const size_t code_at = line.find("code=");
    CHECK(code_at != std::string::npos);
    if (code_at == std::string::npos) continue;
    std::string code = line.substr(code_at + 5);
    const size_t space = code.find(' ');
    if (space != std::string::npos) code = code.substr(0, space);
    bool known = false;
    for (const char* a : allowed) known = known || code == a;
    CHECK(known);
  }
  CHECK(lines > 0);
  diag::Shutdown();
}

// T-02: the commit/pre-edit combinations decide whether the prefix is
// written once, a new composition opens after it, or the original text is
// restored.
void CaseCompositionApplyPlan() {
  using autoime::EditAction;
  using autoime::PlanCompositionApply;
  CHECK(PlanCompositionApply(true, false, true) == EditAction::kCommit);
  CHECK(PlanCompositionApply(true, false, false) ==
        EditAction::kCommitThenCompose);
  CHECK(PlanCompositionApply(false, true, false) == EditAction::kCompose);
  CHECK(PlanCompositionApply(false, true, true) == EditAction::kRestore);
  // An empty commit string never writes anything.
  CHECK(PlanCompositionApply(true, true, true) == EditAction::kRestore);
}

// T-02: emoji and the surrounding text survive UTF-8 -> UTF-16 conversion.
void CaseEmojiConversionKeepsPrefixAndSuffix() {
  // U+7532 U+1F600 U+4E59 = 甲😀乙
  const std::string utf8 = "\xE7\x94\xB2\xF0\x9F\x98\x80\xE4\xB9\x99";
  const std::wstring wide = autoime::Utf8ToUtf16(utf8);
  CHECK(wide.size() == 4);  // 甲 + surrogate pair + 乙
  if (wide.size() == 4) {
    CHECK(wide[0] == 0x7532);
    CHECK(wide[1] == 0xD83D);
    CHECK(wide[2] == 0xDE00);
    CHECK(wide[3] == 0x4E59);
  }
  CHECK(autoime::Utf8ToUtf16("AutoOS").size() == 6);
  CHECK(autoime::Utf8ToUtf16("").empty());
}

void CaseOffsetMapping() {
  using autoime::Utf16OffsetFromUtf8Codepoint;
  CHECK(Utf16OffsetFromUtf8Codepoint("abc", 0) == 0);
  CHECK(Utf16OffsetFromUtf8Codepoint("abc", 2) == 2);
  CHECK(Utf16OffsetFromUtf8Codepoint("abc", 3) == 3);
  CHECK(Utf16OffsetFromUtf8Codepoint("abc", 99) == 3);
  // U+7532 U+1F600 U+4E59 = 甲😀乙
  const std::string emoji = "\xE7\x94\xB2\xF0\x9F\x98\x80\xE4\xB9\x99";
  CHECK(Utf16OffsetFromUtf8Codepoint(emoji, 0) == 0);
  CHECK(Utf16OffsetFromUtf8Codepoint(emoji, 1) == 1);
  CHECK(Utf16OffsetFromUtf8Codepoint(emoji, 2) == 3);  // surrogate pair
  CHECK(Utf16OffsetFromUtf8Codepoint(emoji, 3) == 4);
  CHECK(Utf16OffsetFromUtf8Codepoint(emoji, -1) == 0);
  CHECK(Utf16OffsetFromUtf8Codepoint("", 5) == 0);
}

// PLAN-073 V-07: the production key map must commit the real symbol through
// the real librime engine, not only through the fake target. Resources resolve
// from AUTOIME_RESOURCES_DIR, else from the fixture's own tree.
bool FileExistsA(const std::string& path) {
  const DWORD attrs = GetFileAttributesA(path.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES &&
         !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::string ExeDir() {
  char buf[MAX_PATH]{};
  const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  std::string path(buf, n);
  const size_t slash = path.find_last_of("\\/");
  return slash == std::string::npos ? path : path.substr(0, slash);
}

void RemoveTree(const std::string& dir) {
  WIN32_FIND_DATAA fd{};
  const std::string pattern = dir + "\\*";
  HANDLE find = FindFirstFileA(pattern.c_str(), &fd);
  if (find == INVALID_HANDLE_VALUE) return;
  do {
    const std::string name = fd.cFileName;
    if (name == "." || name == "..") continue;
    const std::string full = dir + "\\" + name;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      RemoveTree(full);
    else
      DeleteFileA(full.c_str());
  } while (FindNextFileA(find, &fd));
  FindClose(find);
  RemoveDirectoryA(dir.c_str());
}

void CaseShiftSymbolsRealEngine() {
  const char* env = std::getenv("AUTOIME_RESOURCES_DIR");
  const std::string resources =
      (env && *env) ? std::string(env)
                    : ExeDir() + "\\..\\..\\..\\shared\\resources";
  if (!FileExistsA(resources + "\\autoime_pinyin.schema.yaml")) {
    std::printf("FAIL shift_symbols_real_engine: resources missing at %s\n",
                resources.c_str());
    CHECK(false);
    return;
  }
  char temp[MAX_PATH]{};
  GetTempPathA(MAX_PATH, temp);
  const std::string user = std::string(temp) + "autoime-fixture-engine-" +
                           std::to_string(GetCurrentProcessId());
  RemoveTree(user);
  RimeEngine engine;
  engine.SetPaths(resources, user);
  std::string error;
  if (!engine.EnsureReady(&error)) {
    std::printf("FAIL shift_symbols_real_engine: engine unavailable: %s\n",
                error.c_str());
    CHECK(false);
    RemoveTree(user);
    return;
  }
  const uintptr_t session = engine.CreateSession();
  CHECK(session != 0);
  struct SymbolCase {
    WPARAM vk;
    bool shift;
    const char* label;
    const char* expect_utf8;  // nullptr = librime opens a symbol composition
  };
  // Frozen by running the production map through librime 1.13.1 on an empty
  // user directory. Keys with a nullptr expectation are still routed and
  // consumed; librime opens a symbol composition instead of committing at once
  // (upstream behaviour, not part of this fix), so no commit is asserted.
  const SymbolCase kSymbols[] = {
      {VK_OEM_1, true, "shift_semicolon", "："},
      {VK_OEM_1, false, "semicolon", "；"},
      {VK_OEM_PLUS, true, "shift_equals", "+"},
      {VK_OEM_2, true, "shift_slash", "？"},
      {VK_OEM_7, true, "shift_quote", "“"},
      {VK_OEM_MINUS, true, "shift_minus", "——"},
      {'1', true, "shift_1", "！"},
      {'9', true, "shift_9", "（"},
      {'0', true, "shift_0", "）"},
      {VK_OEM_2, false, "slash", nullptr},
      {VK_OEM_3, true, "shift_grave", nullptr},
      {VK_OEM_4, true, "shift_bracket", nullptr},
      {VK_OEM_5, true, "shift_backslash", nullptr},
      {VK_OEM_6, true, "shift_rbracket", nullptr},
      {VK_OEM_COMMA, true, "shift_comma", nullptr},
      {VK_OEM_PERIOD, true, "shift_period", nullptr},
      {VK_OEM_PLUS, false, "equals", nullptr},
      {'2', true, "shift_2", nullptr},
  };
  for (const SymbolCase& c : kSymbols) {
    engine.ClearComposition(session);
    const int keysym = KeysymForVk(c.vk, c.shift);
    const int mask = RimeModifierMask(Make(c.vk, c.shift).mods);
    const bool consumed = engine.ProcessKey(session, keysym, mask);
    bool has_commit = false;
    const std::string commit = engine.TakeCommit(session, &has_commit);
    std::printf("ENGINE %s keysym=0x%x mask=%d consumed=%d commit=%s\n",
                c.label, keysym, mask, consumed ? 1 : 0, commit.c_str());
    CHECK(consumed);
    if (c.expect_utf8) {
      CHECK(has_commit);
      CHECK(commit == c.expect_utf8);
    }
  }
  engine.DestroySession(session);
  engine.Shutdown();
  RemoveTree(user);
}

// PLAN-078 T-06: the horizontal candidate bar lays one page out from measured
// text extents, and measuring/painting/hit-testing share the same rects. These
// cases assert the pure geometry the window uses (no visible window needed).
void CaseCandidateLayoutHorizontal() {
  CandidateMetrics m;  // 96-DPI design metrics
  std::vector<SIZE> sizes;
  const int text_widths[] = {24, 48, 72, 36, 60};  // e.g. 是/你好/输入法/...
  for (int w : text_widths) sizes.push_back(SIZE{w, 18});
  const int label_w = 8;
  const CandidateLayout layout =
      ComputeCandidateLayout(sizes, label_w, true, 40, m, 900);
  CHECK(layout.items.size() == 5);
  CHECK(layout.row_count == 1);  // plenty of width: single horizontal row
  CHECK(layout.width <= 900);
  for (size_t i = 0; i < layout.items.size(); ++i) {
    const RECT& r = layout.items[i];
    const int min_w = m.item_pad_x * 2 + label_w + m.label_gap + text_widths[i];
    CHECK(r.right - r.left >= min_w);       // text fits inside its own rect
    CHECK(r.right - r.left <= layout.width);
    CHECK(r.top == layout.items[0].top);     // same row
    CHECK(r.bottom == layout.items[0].bottom);
    if (i > 0) CHECK(r.left >= layout.items[i - 1].right);  // left to right
    const POINT center = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    CHECK(HitTestCandidate(layout, center) == static_cast<int>(i));
  }
  // The pre-edit owns the top row: no candidate rect may overlap it, and a
  // click on it is not a candidate click.
  CHECK(layout.preedit.right > layout.preedit.left);
  CHECK(layout.preedit.bottom <= layout.items[0].top);
  const POINT ped = {(layout.preedit.left + layout.preedit.right) / 2,
                     (layout.preedit.top + layout.preedit.bottom) / 2};
  CHECK(HitTestCandidate(layout, ped) == -1);
  // Padding is not a candidate either.
  const POINT padding = {1, 1};
  CHECK(HitTestCandidate(layout, padding) == -1);
}

// A narrow work area (small window, high DPI, many candidates) wraps the page
// into rows while keeping the page order and the one-rect-per-candidate map.
void CaseCandidateLayoutWrapNarrow() {
  CandidateMetrics m;
  std::vector<SIZE> sizes;
  for (int i = 0; i < 5; ++i) sizes.push_back(SIZE{60, 18});
  const int label_w = 8;
  const int max_width = 240;  // room for two items per row only
  const CandidateLayout layout =
      ComputeCandidateLayout(sizes, label_w, true, 40, m, max_width);
  CHECK(layout.items.size() == 5);
  CHECK(layout.width <= max_width);
  CHECK(layout.row_count >= 3);
  for (size_t i = 0; i < layout.items.size(); ++i) {
    const RECT& r = layout.items[i];
    CHECK(r.left >= (m.border + m.pad_x));
    CHECK(r.right <= layout.width - (m.border + m.pad_x));
    const POINT center = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    CHECK(HitTestCandidate(layout, center) == static_cast<int>(i));
    // Order is preserved and rects never overlap, including across rows.
    for (size_t j = 0; j < i; ++j) {
      const RECT& prev = layout.items[j];
      const bool disjoint = prev.right <= r.left || r.right <= prev.left ||
                            prev.bottom <= r.top || r.bottom <= prev.top;
      CHECK(disjoint);
    }
  }
  // Row count agrees with the number of distinct row tops.
  int distinct_rows = 0;
  int last_top = -1;
  for (const RECT& r : layout.items) {
    if (r.top != last_top) {
      ++distinct_rows;
      last_top = r.top;
    }
  }
  CHECK(distinct_rows == layout.row_count);
}

// PLAN-078 T-06 page-version counterexample: a click from the previous page
// and an out-of-page index must not select anything; the current page does.
void CaseCandidateClickVersionGuard() {
  CHECK(AcceptCandidateClick(7, 7, 0, 5));
  CHECK(AcceptCandidateClick(7, 7, 4, 5));
  CHECK(!AcceptCandidateClick(6, 7, 0, 5));   // stale page version
  CHECK(!AcceptCandidateClick(7, 7, 5, 5));   // index past the page
  CHECK(!AcceptCandidateClick(7, 7, -1, 5));  // no candidate
  CHECK(!AcceptCandidateClick(0, 0, 0, 5));   // no page shown yet
  CHECK(!AcceptCandidateClick(7, 7, 0, 0));   // empty page
}

// PLAN-078 follow-up: a digit over a visible page selects by position. The rule
// is pure so it can be asserted without TSF: this is the path that prevents
// `SQ` + `1` from typing a literal 1 (librime appends digits to uppercase
// table codes).
void CaseCandidateDigitSelection() {
  CHECK(CandidateDigitIndex('1', 5) == 0);
  CHECK(CandidateDigitIndex('5', 5) == 4);
  CHECK(CandidateDigitIndex('1', 1) == 0);   // single candidate still selects
  CHECK(CandidateDigitIndex('2', 1) == -1);  // ... but only its own label
  CHECK(CandidateDigitIndex('1', 0) == -1);
  CHECK(CandidateDigitIndex('0', 5) == -1);
  CHECK(CandidateDigitIndex('6', 5) == -1);
  CHECK(CandidateDigitIndex('a', 5) == -1);
  CHECK(CandidateDigitIndex(0x31, 3) == 0);  // VK '1' path
}
// Metrics scale with the monitor DPI so measuring, painting and clicks stay in
// one coordinate space on every monitor.
void CaseCandidateMetricsScaleWithDpi() {
  const CandidateMetrics base = CandidateMetricsForDpi(96);
  const CandidateMetrics hidpi = CandidateMetricsForDpi(192);
  CHECK(base.row_h == 28);
  CHECK(hidpi.row_h == base.row_h * 2);
  CHECK(hidpi.item_gap == base.item_gap * 2);
  CHECK(hidpi.pad_x == base.pad_x * 2);
  CHECK(hidpi.border >= 1);
  // The same logical page is wider at 192 DPI, and it is still one row when
  // the work area scales with it.
  std::vector<SIZE> sizes(3, SIZE{40, 18});
  const CandidateLayout at96 = ComputeCandidateLayout(sizes, 8, false, 0, base, 600);
  const CandidateLayout at192 =
      ComputeCandidateLayout(std::vector<SIZE>(3, SIZE{80, 36}), 16, false, 0,
                             hidpi, 1200);
  CHECK(at96.row_count == 1);
  CHECK(at192.row_count == 1);
  CHECK(at192.height == at96.height * 2);
}

// PLAN-078 T-06 window-level oracle: render the real popup through
// WM_PRINTCLIENT and click the real window with the layout's own rectangles.
// This is the "click selects exactly what is displayed" check; the layout cases
// above cover ordering and wrapping without a window.
void CaseCandidateWindowRenderAndClick() {
  HINSTANCE inst = GetModuleHandleW(nullptr);
  g_hInst = inst;
  const wchar_t kOwnerClass[] = L"AutoIMEFixtureOwner";
  WNDCLASSW wc = {};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = inst;
  wc.lpszClassName = kOwnerClass;
  RegisterClassW(&wc);
  HWND owner =
      CreateWindowExW(0, kOwnerClass, L"owner", WS_OVERLAPPEDWINDOW, 100, 100,
                      400, 200, nullptr, nullptr, inst, nullptr);
  CHECK(owner != nullptr);

  CandidateWindow window;
  int clicked_index = -1;
  uint64_t clicked_version = 0;
  int click_count = 0;
  window.SetSelectHandler([&](int page_index, uint64_t version) {
    clicked_index = page_index;
    clicked_version = version;
    ++click_count;
  });

  RimeEngine::Snapshot snap;
  snap.candidates = {L"是", L"你好", L"高斯泼溅", L"SQLite", L"候选窗口"};
  snap.labels = {L"1", L"2", L"3", L"4", L"5"};
  snap.highlighted = 2;
  snap.version = 42;
  snap.is_composing = true;
  snap.preedit_utf16 = L"gao si po jian";
  snap.page_no = 0;
  const RECT caret = {400, 400, 500, 420};
  CHECK(window.EnsureCreated(owner));
  window.Show(snap, caret);
  CHECK(window.visible());
  CHECK(window.version() == 42);
  const CandidateLayout& layout = window.layout();
  RECT client = {};
  GetClientRect(window.hwnd(), &client);
  CHECK(client.right == layout.width);
  CHECK(client.bottom == layout.height);
  CHECK(layout.items.size() == snap.candidates.size());
  CHECK(layout.row_count >= 1);
  // The popup never takes activation: focus stays with the editor.
  CHECK(GetForegroundWindow() != window.hwnd());
  // It is also kept inside the work area, including when the caret sits at the
  // bottom-right edge of the screen.
  auto inside_work_area = [&]() {
    RECT wr = {};
    GetWindowRect(window.hwnd(), &wr);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(window.hwnd(), MONITOR_DEFAULTTONEAREST),
                    &mi);
    return wr.left >= mi.rcWork.left && wr.top >= mi.rcWork.top &&
           wr.right <= mi.rcWork.right && wr.bottom <= mi.rcWork.bottom;
  };
  CHECK(inside_work_area());
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfoW(MonitorFromPoint({caret.left, caret.top},
                                   MONITOR_DEFAULTTONEAREST),
                  &mi);
  const RECT edge_caret = {mi.rcWork.right - 4, mi.rcWork.bottom - 4,
                           mi.rcWork.right - 2, mi.rcWork.bottom - 2};
  window.Show(snap, edge_caret);
  CHECK(window.visible());
  CHECK(inside_work_area());
  window.Show(snap, caret);
  CHECK(window.visible());

  // Render the real paint path into a 32-bpp top-down DIB.
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = layout.width;
  bi.bmiHeader.biHeight = -layout.height;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  CHECK(dib != nullptr && bits != nullptr);
  HGDIOBJ old_bmp = SelectObject(mem, dib);
  SendMessageW(window.hwnd(), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(mem),
               PRF_CLIENT);
  auto pixel = [&](int x, int y) -> COLORREF {
    const BYTE* p = static_cast<const BYTE*>(bits) +
                    (static_cast<size_t>(y) * layout.width + x) * 4;
    return RGB(p[2], p[1], p[0]);  // BGRA in memory
  };
  // PLAN-078 follow-up: 2 px rounded frame. Sample the edge middles (the
  // corners are clipped by the round window region).
  CHECK(pixel(layout.width / 2, 0) == RGB(0x2B, 0x6C, 0xF0));
  CHECK(pixel(layout.width / 2, 1) == RGB(0x2B, 0x6C, 0xF0));
  CHECK(pixel(0, layout.height / 2) == RGB(0x2B, 0x6C, 0xF0));
  CHECK(pixel(layout.width - 1, layout.height / 2) == RGB(0x2B, 0x6C, 0xF0));
  CHECK(pixel(layout.width - 3, layout.preedit.top + 2) ==
        RGB(0xE8, 0xF0, 0xFA));  // pale blue background
  const RECT& highlighted = layout.items[2];
  CHECK(pixel(highlighted.left + 2, (highlighted.top + highlighted.bottom) / 2) ==
        RGB(0x2B, 0x6C, 0xF0));  // selected item keeps its accent fill
  const RECT& unselected = layout.items[3];
  CHECK(pixel(unselected.left + 2, (unselected.top + unselected.bottom) / 2) ==
        RGB(0xE8, 0xF0, 0xFA));  // ordinary item leaves the background
  SelectObject(mem, old_bmp);
  DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);

  // Clicking candidate 4 (page index 3) selects exactly that index and version.
  auto click = [&](POINT pt) {
    SendMessageW(window.hwnd(), WM_LBUTTONUP, 0,
                 MAKELPARAM(static_cast<WORD>(pt.x), static_cast<WORD>(pt.y)));
  };
  const RECT& target = layout.items[3];
  click(POINT{(target.left + target.right) / 2, (target.top + target.bottom) / 2});
  CHECK(click_count == 1);
  CHECK(clicked_index == 3);
  CHECK(clicked_version == 42);
  // The pre-edit row, the padding and the area below the page are not targets.
  click(POINT{(layout.preedit.left + layout.preedit.right) / 2,
              (layout.preedit.top + layout.preedit.bottom) / 2});
  click(POINT{2, 2});
  click(POINT{layout.width / 2, layout.height + 5});
  CHECK(click_count == 1);

  window.Hide();
  CHECK(!window.visible());
  DestroyWindow(owner);
}

long long NowUs();

// PLAN-078 T-03: the production adapter must run against the per-user owner
// over the real pipe, not only in-process. This case spawns
// autoime_engine.exe (isolated instance + temp user dir), types through the
// production RimeEngine remote mode, selects a candidate, takes exactly one
// commit and confirms it, then checks the T-01 admission rule through the same
// path and stops the owner it started.
void CaseRemoteEngineSession() {
  const char* env = std::getenv("AUTOIME_RESOURCES_DIR");
  const std::string resources =
      (env && *env) ? std::string(env)
                    : ExeDir() + "\\..\\..\\..\\shared\\resources";
  if (!FileExistsA(resources + "\\autoime_pinyin.schema.yaml")) {
    std::printf("FAIL remote_engine_session: resources missing at %s\n",
                resources.c_str());
    CHECK(false);
    return;
  }
  char temp[MAX_PATH]{};
  GetTempPathA(MAX_PATH, temp);
  char instance[64];
  std::snprintf(instance, sizeof(instance), "fixture-%lu", GetCurrentProcessId());
  const std::string user =
      std::string(temp) + "autoime-fixture-remote-" + instance;
  RemoveTree(user);
  RimeEngine engine;
  engine.SetModuleDir(ExeDir());
  engine.SetRemote(true);
  engine.SetRemoteInstance(instance);
  engine.SetPaths(resources, user);
  std::string error;
  const long long start = NowUs();
  if (!engine.EnsureReady(&error)) {
    std::printf("FAIL remote_engine_session: owner unavailable: %s\n",
                error.c_str());
    CHECK(false);
    engine.ShutdownOwner(&error);
    RemoveTree(user);
    return;
  }
  const long long ready_us = NowUs() - start;
  const uintptr_t session = engine.CreateSession();
  CHECK(session != 0);
  auto type = [&](const char* keys) {
    for (const char* p = keys; *p; ++p) {
      CHECK(engine.ProcessKey(session, static_cast<int>(*p), 0));
    }
  };
  auto first_candidate = [&](RimeEngine::Snapshot* out) {
    return engine.GetContext(session, out) && !out->candidates.empty();
  };
  type("nihao");
  RimeEngine::Snapshot snapshot;
  const bool page = first_candidate(&snapshot);
  CHECK(page);
  if (page) {
    CHECK(snapshot.candidates[0] == L"你好");
    CHECK(snapshot.version != 0);
    const int index = 0;
    CHECK(engine.SelectCandidateOnPage(session,
                                       static_cast<size_t>(index)));
    bool has_commit = false;
    const std::string commit = engine.TakeCommit(session, &has_commit);
    CHECK(has_commit);
    CHECK(commit == "你好");
    CHECK(engine.ConfirmCommit(session));
    // PLAN-078 review F-01: the TSF forgets a commit without resending the
    // code; the owner replays the remembered one and a miss is benign (it must
    // not tear the session down).
    CHECK(engine.ForgetCommit(session, commit));
    CHECK(engine.ready());
    CHECK(engine.ForgetCommit(session, "not-a-candidate"));
    CHECK(engine.ready());
    bool has_second = true;
    engine.TakeCommit(session, &has_second);
    CHECK(!has_second);  // one commit per confirmation
  }
  // PLAN-078 T-01 through the shared path: `s` is Chinese, SQLite is absent.
  engine.ClearComposition(session);
  type("s");
  RimeEngine::Snapshot single;
  const bool single_page = first_candidate(&single);
  CHECK(single_page);
  if (single_page) {
    CHECK(single.candidates[0] == L"是");
    for (const std::wstring& candidate : single.candidates) {
      CHECK(candidate != L"SQLite");
    }
  }
  engine.DestroySession(session);
  std::printf("REMOTE ready_ms=%.1f first=\"%s\"\n", ready_us / 1000.0,
              single_page ? "是" : "?");
  std::string owner_error;
  CHECK(engine.ShutdownOwner(&owner_error));
  engine.Shutdown();
  for (int i = 0; i < 20; ++i) {
    RemoveTree(user);
    if (GetFileAttributesA(user.c_str()) == INVALID_FILE_ATTRIBUTES) break;
    Sleep(100);
  }
}

// PLAN-078 T-04: a confirmation of a supplementary word (autoime_words: 是的)
// must land in the SAME user dictionary as ordinary pinyin, while a cancelled
// composition must write nothing. The oracle is the on-disk user dictionary
// size after the owner finalizes (librime flushes on finalize), so it does not
// depend on rank: 是的 is already first on its shipped weight.
long long DirSize(const std::string& dir) {
  WIN32_FIND_DATAA fd{};
  HANDLE find = FindFirstFileA((dir + "\\*").c_str(), &fd);
  if (find == INVALID_HANDLE_VALUE) return 0;
  long long total = 0;
  do {
    const std::string name = fd.cFileName;
    if (name == "." || name == "..") continue;
    const std::string full = dir + "\\" + name;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      total += DirSize(full);
    } else {
      LARGE_INTEGER size{};
      size.LowPart = fd.nFileSizeLow;
      size.HighPart = static_cast<LONG>(fd.nFileSizeHigh);
      total += size.QuadPart;
    }
  } while (FindNextFileA(find, &fd));
  FindClose(find);
  return total;
}

void CaseSupplementLearningSharedDb() {
  const char* env = std::getenv("AUTOIME_RESOURCES_DIR");
  const std::string resources =
      (env && *env) ? std::string(env)
                    : ExeDir() + "\\..\\..\\..\\shared\\resources";
  if (!FileExistsA(resources + "\\autoime_pinyin.schema.yaml")) {
    std::printf("FAIL supplement_learning_shared_db: resources missing at %s\n",
                resources.c_str());
    CHECK(false);
    return;
  }
  char temp[MAX_PATH]{};
  GetTempPathA(MAX_PATH, temp);
  char instance[64];
  std::snprintf(instance, sizeof(instance), "fit-supp-%lu",
                GetCurrentProcessId());
  const std::string user = std::string(temp) + "autoime-fixture-supp-" + instance;
  RemoveTree(user);

  auto open = [&](RimeEngine* engine) {
    engine->SetModuleDir(ExeDir());
    engine->SetRemote(true);
    engine->SetRemoteInstance(instance);
    engine->SetPaths(resources, user);
    std::string error;
    if (!engine->EnsureReady(&error)) {
      std::printf("FAIL supplement_learning_shared_db: owner unavailable: %s\n",
                  error.c_str());
      return false;
    }
    return true;
  };
  auto type = [](RimeEngine* engine, uintptr_t session, const char* keys) {
    for (const char* p = keys; *p; ++p) {
      engine->ProcessKey(session, static_cast<int>(*p), 0);
    }
  };
  auto find_yes = [](RimeEngine* engine, uintptr_t session, int* index) {
    RimeEngine::Snapshot snapshot;
    if (!engine->GetContext(session, &snapshot)) return false;
    for (size_t i = 0; i < snapshot.candidates.size(); ++i) {
      if (snapshot.candidates[i] == L"是的") {
        *index = static_cast<int>(i);
        return true;
      }
    }
    return false;
  };

  RimeEngine engine;
  if (!open(&engine)) {
    RemoveTree(user);
    return;
  }
  const long long baseline = DirSize(user);  // deployed artifacts, no learning
  uintptr_t session = engine.CreateSession();
  bool confirmed = false;
  if (session != 0) {
    // Ten confirmations so the user-dictionary write dominates open/finalize
    // log churn (one entry is only tens of bytes on disk).
    int commits = 0;
    for (int i = 0; i < 10; ++i) {
      engine.ClearComposition(session);
      type(&engine, session, "sd");
      int index = -1;
      if (!find_yes(&engine, session, &index)) break;
      if (!engine.SelectCandidateOnPage(session, static_cast<size_t>(index)))
        break;
      bool has = false;
      const std::string commit = engine.TakeCommit(session, &has);
      if (!has || commit != "是的") break;
      if (!engine.ConfirmCommit(session)) break;
      ++commits;
    }
    confirmed = commits == 10;
    engine.DestroySession(session);
  }
  std::string owner_error;
  CHECK(engine.ShutdownOwner(&owner_error));
  engine.Shutdown();
  Sleep(200);
  const long long after_commit = DirSize(user);

  RimeEngine engine2;
  bool cancelled = false;
  if (open(&engine2)) {
    const uintptr_t session2 = engine2.CreateSession();
    if (session2 != 0) {
      engine2.ClearComposition(session2);
      type(&engine2, session2, "sd");
      engine2.ClearComposition(session2);  // Esc/no-commit path
      bool has = true;
      engine2.TakeCommit(session2, &has);
      cancelled = !has;
      engine2.DestroySession(session2);
    }
    CHECK(engine2.ShutdownOwner(&owner_error));
    engine2.Shutdown();
  }
  Sleep(200);
  const long long after_cancel = DirSize(user);
  const long long learned_delta = after_commit - baseline;
  const long long cancel_delta = after_cancel - after_commit;
  std::printf("SUPP baseline=%lld after_commit=%lld after_cancel=%lld "
              "learned_delta=%lld cancel_delta=%lld\n",
              baseline, after_commit, after_cancel, learned_delta,
              cancel_delta);
  CHECK(confirmed);
  CHECK(cancelled);
  // Ten confirmations write well over 500 bytes into the leveldb log; a
  // cancelled composition leaves the size essentially unchanged (small
  // open/finalize churn may even shrink it through log rotation).
  CHECK(learned_delta > 500);
  // PLAN-078 review F-05: one learned entry is ~1/10 of the ten-confirmation
  // delta, so the tighter bound still catches a spurious single learn.
  CHECK(cancel_delta < learned_delta / 10);
  CHECK(cancel_delta < 100);
  for (int i = 0; i < 20; ++i) {
    RemoveTree(user);
    if (GetFileAttributesA(user.c_str()) == INVALID_FILE_ATTRIBUTES) break;
    Sleep(100);
  }
}

long long NowUs() {
  static LARGE_INTEGER freq{};
  if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return freq.QuadPart ? now.QuadPart * 1000000 / freq.QuadPart : 0;
}

struct CaseEntry {
  const char* name;
  void (*run)();
};

}  // namespace

int main() {
  SetConsoleOutputCP(CP_UTF8);
  const CaseEntry cases[] = {
      {"repeated_test", CaseRepeatedTestProcessesOnce},
      {"down_only", CaseDownOnlyProcessesOnce},
      {"auto_repeat", CaseAutoRepeatProcessesEachPress},
      {"stale_focus", CaseStaleFocusCallbackDropped},
      {"shortcuts", CaseShortcutsStayWithHost},
      {"shift_click", CaseShiftClickTogglesMode},
      {"shift_click_cancel", CaseShiftClickCancelsComposition},
      {"sensitive_unready", CaseSensitiveAndUnreadyPassThrough},
      {"ascii_mode", CaseAsciiModeTypesDirectly},
      {"idle_keys", CaseIdleKeysStayWithHost},
      {"composing_keys", CaseComposingKeysAreConsumed},
      {"punctuation", CasePunctuationFollowsMode},
      {"keysym_mapping", CaseGeneratedKeysymMapping},
      {"shift_symbol_mapping", CaseShiftSymbolMapping},
      {"shift_symbol_routing", CaseShiftSymbolRouting},
      {"shift_test_only_no_toggle", CaseShiftTestOnlyDoesNotToggleMode},
      {"shift_symbols_real_engine", CaseShiftSymbolsRealEngine},
      {"composition_plan", CaseCompositionApplyPlan},
      {"emoji_conversion", CaseEmojiConversionKeepsPrefixAndSuffix},
      {"offset_mapping", CaseOffsetMapping},
      {"candidate_layout_horizontal", CaseCandidateLayoutHorizontal},
      {"candidate_layout_wrap_narrow", CaseCandidateLayoutWrapNarrow},
      {"candidate_click_version", CaseCandidateClickVersionGuard},
      {"candidate_metrics_dpi", CaseCandidateMetricsScaleWithDpi},
      {"candidate_digit_selection", CaseCandidateDigitSelection},
      {"candidate_window_render_click", CaseCandidateWindowRenderAndClick},
      {"remote_engine_session", CaseRemoteEngineSession},
      {"supplement_learning_shared_db", CaseSupplementLearningSharedDb},
      {"diagnostics_payload_free", CaseDiagnosticsArePayloadFree},
  };
  long long total_us = 0;
  for (const CaseEntry& entry : cases) {
    const int before = g_failures;
    const long long start = NowUs();
    entry.run();
    const long long elapsed = NowUs() - start;
    total_us += elapsed;
    std::printf("CASE %s ms=%.3f failures=%d\n", entry.name,
                elapsed / 1000.0, g_failures - before);
  }

  std::printf("SUMMARY {\"fixture\": \"key_router\", \"cases\": %zu, "
              "\"checks\": %d, \"failures\": %d, \"total_ms\": %.3f}\n",
              sizeof(cases) / sizeof(cases[0]), g_checks, g_failures,
              total_us / 1000.0);
  return g_failures == 0 ? 0 : 5;
}
