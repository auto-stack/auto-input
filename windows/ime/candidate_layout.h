// AutoIME - horizontal candidate geometry (PLAN-078 T-06).
//
// One page is laid out left to right after the pre-edit row. The very same
// rectangles are used for measuring, painting, highlighting and mouse
// hit-testing, so the item drawn under the pointer is the item the engine
// selects. Layout is pure geometry (no window, no GDI), which lets the
// automated fixture assert rects, wrapping and click mapping headlessly.
#ifndef AUTOIME_CANDIDATE_LAYOUT_H_
#define AUTOIME_CANDIDATE_LAYOUT_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include <windows.h>

// Scaled from the 96-DPI design metrics with MulDiv.
struct CandidateMetrics {
  int row_h = 28;      // pre-edit row height (also the item row height)
  int pad_x = 10;      // padding between the frame and the content
  int pad_y = 6;
  int label_gap = 6;   // label -> text gap inside one item
  int item_gap = 8;    // gap between two items on the same row
  int item_pad_x = 6;  // horizontal padding inside one item rect
  int border = 2;      // frame around the popup (2 px reads clearly)
  int corner_radius = 4;  // rounded popup corners (DPI scaled)
  int preedit_gap = 2; // pre-edit row -> first candidate row
  int margin = 24;     // keep the popup away from the work-area edges
};

CandidateMetrics CandidateMetricsForDpi(unsigned dpi);

struct CandidateLayout {
  int width = 0;
  int height = 0;
  int row_count = 0;
  bool has_preedit = false;
  RECT preedit = {0, 0, 0, 0};  // empty (all zero) when there is no pre-edit
  std::vector<RECT> items;      // one rect per candidate, display order
};

// Lays out one page of candidates. `item_sizes` are the measured text extents
// of each candidate, `label_width` the widest label. Candidates flow left to
// right and wrap to the next row when they would exceed `max_width`; a single
// candidate wider than that is clipped to the available width (its rect is
// still the click target). Rects are window-local, origin (0,0).
CandidateLayout ComputeCandidateLayout(const std::vector<SIZE>& item_sizes,
                                       int label_width, bool has_preedit,
                                       int preedit_width,
                                       const CandidateMetrics& m,
                                       int max_width);

// Window-local hit test. Returns the page index, or -1 for padding, the
// pre-edit row and anything outside the popup.
int HitTestCandidate(const CandidateLayout& layout, POINT pt);

// PLAN-078 follow-up: the page index a digit key selects, or -1. The TSF layer
// uses the page the user sees (librime appends digits to some compositions,
// e.g. explicit uppercase table codes), so this rule is pure and unit-tested.
int CandidateDigitIndex(int keysym, size_t candidate_count);

// A click is accepted only when it carries the page version the popup was laid
// out for (a stale page must not commit another candidate) and its index
// exists on that page.
bool AcceptCandidateClick(uint64_t click_version, uint64_t current_version,
                          int page_index, size_t candidate_count);

#endif  // AUTOIME_CANDIDATE_LAYOUT_H_
