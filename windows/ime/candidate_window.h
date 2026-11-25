// AutoIME - native Win32 candidate window (PLAN-069 T-03, PLAN-070 T-03/F4,
// PLAN-078 T-06 horizontal bar).
// Popup that never takes focus (WS_EX_NOACTIVATE, owner = editor window), so
// the editor keeps keyboard focus while candidates are visible. It renders the
// pre-edit line on its own top row and the page's candidates left to right,
// using the engine's real selection labels, per-monitor DPI metrics, a thin
// blue frame on a pale blue-white background and mouse selection. Geometry
// comes from candidate_layout: measuring, painting, highlighting and
// hit-testing share the same rectangles. A page version guards stale clicks. A
// short timer asks the text service to re-query the caret so the popup follows
// window moves/zoom without a keypress.
#ifndef AUTOIME_CANDIDATE_WINDOW_H_
#define AUTOIME_CANDIDATE_WINDOW_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <windows.h>

#include "candidate_layout.h"
#include "rime_engine.h"

class CandidateWindow {
 public:
  using SelectHandler = std::function<void(int page_index, uint64_t version)>;
  using RefreshHandler = std::function<void()>;

  ~CandidateWindow();
  // Creates the window (once per process) owned by `owner`.
  bool EnsureCreated(HWND owner);
  // Shows the current page's candidates anchored at `caret` (screen coords).
  void Show(const RimeEngine::Snapshot& snap, const RECT& caret);
  void Hide();
  bool visible() const { return wnd_ != nullptr && visible_; }
  void SetSelectHandler(SelectHandler handler) { handler_ = std::move(handler); }
  // Called on the reposition timer while the popup is visible.
  void SetRefreshHandler(RefreshHandler handler) {
    refresh_ = std::move(handler);
  }
  uint64_t version() const { return version_; }
  // Read-only accessors for diagnostics and the automated fixture: the live
  // popup window and the geometry of the page currently shown.
  HWND hwnd() const { return wnd_; }
  const CandidateLayout& layout() const { return layout_; }

 private:
  static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);
  void EnsureFont(UINT dpi);
  void Layout(const RimeEngine::Snapshot& snap, const RECT& caret);
  void Paint(HDC dc);
  void OnClick(POINT pt);

  HWND wnd_ = nullptr;
  HFONT font_ = nullptr;
  UINT font_dpi_ = 0;
  bool visible_ = false;
  bool has_preedit_ = false;
  std::wstring preedit_;
  std::vector<std::wstring> items_;   // candidate texts, display order
  std::vector<std::wstring> labels_;  // engine-provided selection labels
  std::vector<SIZE> item_sizes_;      // measured text extents
  CandidateMetrics metrics_;
  CandidateLayout layout_;
  int label_w_ = 0;  // widest measured label
  int highlighted_ = 0;
  bool last_page_ = true;
  int page_no_ = 0;
  uint64_t version_ = 0;
  SelectHandler handler_;
  RefreshHandler refresh_;
};

#endif  // AUTOIME_CANDIDATE_WINDOW_H_
