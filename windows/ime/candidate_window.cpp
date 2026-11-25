// AutoIME - native Win32 candidate window implementation (PLAN-069 T-03,
// PLAN-070 T-03/F4, PLAN-078 T-06: horizontal candidates, shared rects).
#include "candidate_window.h"

#include "diag.h"
#include "ime.h"

namespace {
constexpr wchar_t kWndClass[] = L"AutoIMECandidateWindow";
// PLAN-078 T-06: pale blue-white background with a thin blue frame; the
// highlighted item keeps a solid accent fill so keyboard and mouse selection
// look the same on light and dark host backgrounds.
constexpr COLORREF kBackground = RGB(0xE8, 0xF0, 0xFA);  // pale blue tint
constexpr COLORREF kAccent = RGB(0x2B, 0x6C, 0xF0);
constexpr COLORREF kText = RGB(0x20, 0x20, 0x20);
constexpr COLORREF kPreeditText = RGB(0x50, 0x50, 0x50);
constexpr UINT kRepositionTimer = 1;
constexpr UINT kRepositionMs = 250;

int Scale(int value, UINT dpi) {
  return MulDiv(value, static_cast<int>(dpi ? dpi : 96), 96);
}
}  // namespace

CandidateWindow::~CandidateWindow() {
  if (wnd_) DestroyWindow(wnd_);
  if (font_) DeleteObject(font_);
}

bool CandidateWindow::EnsureCreated(HWND owner) {
  if (wnd_) {
    // Re-owner to the current editor so the popup follows the focus host.
    SetWindowLongPtrW(wnd_, GWLP_HWNDPARENT,
                      reinterpret_cast<LONG_PTR>(owner));
    return true;
  }
  WNDCLASSW wc = {};
  wc.lpfnWndProc = &CandidateWindow::WndProc;
  wc.hInstance = g_hInst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = CreateSolidBrush(kBackground);
  wc.lpszClassName = kWndClass;
  RegisterClassW(&wc);

  wnd_ = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kWndClass, L"",
      WS_POPUP, 0, 0, 10, 10, owner, nullptr, g_hInst, this);
  return wnd_ != nullptr;
}

void CandidateWindow::EnsureFont(UINT dpi) {
  if (font_ && font_dpi_ == dpi) return;
  if (font_) {
    DeleteObject(font_);
    font_ = nullptr;
  }
  font_ = CreateFontW(-Scale(17, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                      L"Microsoft YaHei UI");
  font_dpi_ = dpi;
}

void CandidateWindow::Layout(const RimeEngine::Snapshot& snap,
                             const RECT& caret) {
  items_ = snap.candidates;
  labels_.clear();
  highlighted_ = snap.highlighted;
  last_page_ = snap.is_last_page;
  page_no_ = snap.page_no;
  has_preedit_ = !snap.preedit_utf16.empty();
  preedit_ = snap.preedit_utf16;
  const UINT dpi = wnd_ ? GetDpiForWindow(wnd_) : 0;
  const UINT effective_dpi = dpi ? dpi : 96;
  EnsureFont(effective_dpi);
  metrics_ = CandidateMetricsForDpi(effective_dpi);

  // Measure every candidate and label once; the same extents feed the layout.
  HDC dc = GetDC(wnd_);
  HFONT old = static_cast<HFONT>(SelectObject(dc, font_));
  SIZE sz = {};
  item_sizes_.clear();
  int label_max = 0;
  for (size_t i = 0; i < items_.size(); ++i) {
    GetTextExtentPoint32W(dc, items_[i].c_str(),
                          static_cast<int>(items_[i].size()), &sz);
    item_sizes_.push_back(sz);
    std::wstring label =
        (i < snap.labels.size() && !snap.labels[i].empty())
            ? snap.labels[i]
            : std::to_wstring(i + 1);
    SIZE label_sz = {};
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()),
                          &label_sz);
    if (label_sz.cx > label_max) label_max = label_sz.cx;
    labels_.push_back(std::move(label));
  }
  int preedit_w = 0;
  if (has_preedit_) {
    GetTextExtentPoint32W(dc, preedit_.c_str(),
                          static_cast<int>(preedit_.size()), &sz);
    preedit_w = sz.cx;
  }
  SelectObject(dc, old);
  ReleaseDC(wnd_, dc);
  label_w_ = label_max;

  HMONITOR mon =
      MonitorFromPoint({caret.left, caret.top}, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfoW(mon, &mi);
  const int work_w = mi.rcWork.right - mi.rcWork.left;
  int max_width = work_w - metrics_.margin * 2;
  if (max_width < Scale(60, effective_dpi)) max_width = Scale(60, effective_dpi);

  layout_ = ComputeCandidateLayout(item_sizes_, label_max, has_preedit_,
                                   preedit_w, metrics_, max_width);

  const int gap = Scale(2, effective_dpi);
  const int edge = metrics_.margin;
  int x = caret.left;
  int y = caret.bottom + gap;
  if (x + layout_.width > mi.rcWork.right - edge)
    x = mi.rcWork.right - edge - layout_.width;
  if (x < mi.rcWork.left + edge) x = mi.rcWork.left + edge;
  if (y + layout_.height > mi.rcWork.bottom - edge)
    y = caret.top - layout_.height - gap;
  if (y < mi.rcWork.top + edge) y = mi.rcWork.top + edge;
  ImeLog("cand layout items=%zu rows=%d ped=%d page=%d dpi=%u w=%d h=%d at "
         "(%d,%d)",
         layout_.items.size(), layout_.row_count, has_preedit_ ? 1 : 0,
         page_no_, effective_dpi, layout_.width, layout_.height, x, y);
  SetWindowPos(wnd_, HWND_TOPMOST, x, y, layout_.width, layout_.height,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  // PLAN-078 follow-up: rounded popup corners (radius ~4 px, DPI scaled). The
  // window owns the region after SetWindowRgn.
  const int diameter = metrics_.corner_radius * 2;
  HRGN region = CreateRoundRectRgn(0, 0, layout_.width + 1, layout_.height + 1,
                                   diameter, diameter);
  if (region) SetWindowRgn(wnd_, region, TRUE);
  visible_ = true;
}

void CandidateWindow::Show(const RimeEngine::Snapshot& snap,
                           const RECT& caret) {
  if (!wnd_ || snap.candidates.empty()) {
    Hide();
    return;
  }
  version_ = snap.version;
  Layout(snap, caret);
  SetTimer(wnd_, kRepositionTimer, kRepositionMs, nullptr);
  InvalidateRect(wnd_, nullptr, TRUE);
}

void CandidateWindow::Hide() {
  if (wnd_) {
    KillTimer(wnd_, kRepositionTimer);
    if (visible_) ShowWindow(wnd_, SW_HIDE);
  }
  visible_ = false;
}

void CandidateWindow::Paint(HDC dc) {
  RECT rc;
  GetClientRect(wnd_, &rc);
  // PLAN-078 follow-up: 2 px rounded frame with a ~4 px radius, drawn as a
  // filled accent ring + background inset. Pen geometry would make the exact
  // border width depend on cosmetic-pen anchoring; the ring is exact.
  const int border = metrics_.border > 0 ? metrics_.border : 2;
  const int diameter = metrics_.corner_radius * 2;
  const int inner_diameter = diameter - border * 2;
  HBRUSH accent = CreateSolidBrush(kAccent);
  HBRUSH background = CreateSolidBrush(kBackground);
  HGDIOBJ old_brush = SelectObject(dc, accent);
  HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
  RoundRect(dc, 0, 0, rc.right + 1, rc.bottom + 1, diameter, diameter);
  SelectObject(dc, background);
  RoundRect(dc, border, border, rc.right + 1 - border, rc.bottom + 1 - border,
            inner_diameter > 0 ? inner_diameter : 0,
            inner_diameter > 0 ? inner_diameter : 0);
  SelectObject(dc, old_pen);
  SelectObject(dc, old_brush);
  DeleteObject(accent);
  DeleteObject(background);
  SetBkMode(dc, TRANSPARENT);
  SelectObject(dc, font_);
  TEXTMETRICW tm = {};
  GetTextMetricsW(dc, &tm);
  const int text_dy = (metrics_.row_h - tm.tmHeight) / 2;

  if (has_preedit_ && layout_.preedit.right > layout_.preedit.left) {
    SetTextColor(dc, kPreeditText);
    RECT clip = layout_.preedit;
    ExtTextOutW(dc, clip.left + metrics_.item_pad_x, clip.top + text_dy,
                ETO_CLIPPED, &clip, preedit_.c_str(),
                static_cast<UINT>(preedit_.size()), nullptr);
  }

  const HBRUSH highlight = CreateSolidBrush(kAccent);
  for (size_t i = 0; i < layout_.items.size() && i < items_.size(); ++i) {
    const RECT rect = layout_.items[i];
    if (static_cast<int>(i) == highlighted_) {
      FillRect(dc, &rect, highlight);
      SetTextColor(dc, RGB(0xFF, 0xFF, 0xFF));
    } else {
      SetTextColor(dc, kText);
    }
    // Clip both texts to the shared item rect: an over-long term cannot draw
    // outside its own click target.
    RECT clip = rect;
    int x = rect.left + metrics_.item_pad_x;
    if (i < labels_.size()) {
      ExtTextOutW(dc, x, rect.top + text_dy, ETO_CLIPPED, &clip,
                  labels_[i].c_str(), static_cast<UINT>(labels_[i].size()),
                  nullptr);
      x += label_w_ + metrics_.label_gap;
    }
    ExtTextOutW(dc, x, rect.top + text_dy, ETO_CLIPPED, &clip,
                items_[i].c_str(), static_cast<UINT>(items_[i].size()),
                nullptr);
  }
  DeleteObject(highlight);
}

void CandidateWindow::OnClick(POINT pt) {
  const int index = HitTestCandidate(layout_, pt);
  if (index < 0) return;  // padding or the pre-edit row: not a candidate
  diag::Event("candidate", "click");
  if (handler_) handler_(index, version_);
}

LRESULT CALLBACK CandidateWindow::WndProc(HWND wnd, UINT msg, WPARAM wparam,
                                          LPARAM lparam) {
  auto* self = reinterpret_cast<CandidateWindow*>(
      GetWindowLongPtrW(wnd, GWLP_USERDATA));
  switch (msg) {
    case WM_NCCREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      SetWindowLongPtrW(wnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
      return DefWindowProcW(wnd, msg, wparam, lparam);
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(wnd, &ps);
      if (self) self->Paint(dc);
      EndPaint(wnd, &ps);
      return 0;
    }
    case WM_PRINTCLIENT: {
      // Lets the fixture (and any host that prints the window) render the real
      // paint path into a bitmap, so pixels and click rects share one source.
      if (self) self->Paint(reinterpret_cast<HDC>(wparam));
      return 0;
    }
    case WM_LBUTTONUP: {
      const POINTS pts = MAKEPOINTS(lparam);
      if (self) self->OnClick(POINT{pts.x, pts.y});
      return 0;
    }
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;  // never steal editor focus
    case WM_TIMER:
      if (self && wparam == kRepositionTimer && self->visible_ &&
          self->refresh_)
        self->refresh_();
      return 0;
    case WM_DPICHANGED:
      InvalidateRect(wnd, nullptr, TRUE);
      return 0;
    default:
      return DefWindowProcW(wnd, msg, wparam, lparam);
  }
}
