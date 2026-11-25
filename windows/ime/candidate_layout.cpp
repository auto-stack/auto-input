// AutoIME - horizontal candidate geometry implementation (PLAN-078 T-06).
#include "candidate_layout.h"

namespace {

int ScaleMetric(int value, unsigned dpi) {
  return MulDiv(value, static_cast<int>(dpi ? dpi : 96), 96);
}

int Clamp(int value, int lo, int hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

}  // namespace

CandidateMetrics CandidateMetricsForDpi(unsigned dpi) {
  CandidateMetrics m;
  m.row_h = ScaleMetric(28, dpi);
  m.pad_x = ScaleMetric(10, dpi);
  m.pad_y = ScaleMetric(6, dpi);
  m.label_gap = ScaleMetric(6, dpi);
  m.item_gap = ScaleMetric(8, dpi);
  m.item_pad_x = ScaleMetric(6, dpi);
  m.border = ScaleMetric(2, dpi);  // PLAN-078 follow-up: 2 px reads clearly
  m.corner_radius = ScaleMetric(4, dpi);
  m.preedit_gap = ScaleMetric(2, dpi);
  m.margin = ScaleMetric(24, dpi);
  return m;
}

CandidateLayout ComputeCandidateLayout(const std::vector<SIZE>& item_sizes,
                                       int label_width, bool has_preedit,
                                       int preedit_width,
                                       const CandidateMetrics& m,
                                       int max_width) {
  CandidateLayout out;
  out.has_preedit = has_preedit;
  const int frame_x = (m.border + m.pad_x) * 2;
  const int inner = Clamp(max_width - frame_x, 1, max_width > 0 ? max_width : 1);
  const int label_block = m.item_pad_x * 2 + (label_width > 0 ? label_width : 0) +
                          (label_width > 0 ? m.label_gap : 0);
  const int base_y = m.border + m.pad_y;

  // One width per item: padding + label block + measured text, clipped to the
  // usable width so an over-long term still gets a deterministic rectangle.
  std::vector<int> widths;
  widths.reserve(item_sizes.size());
  int min_item = label_block > 0 ? label_block : 1;
  if (min_item > inner) min_item = inner;
  for (const SIZE& size : item_sizes) {
    const int natural = label_block + (size.cx > 0 ? size.cx : 0);
    widths.push_back(Clamp(natural, min_item, inner));
  }

  // Greedy left-to-right rows. Order is display order, so a click index is the
  // engine page index.
  std::vector<int> row_widths;
  std::vector<size_t> row_start;  // first item of each row
  int row_w = 0;
  size_t row_first = 0;
  for (size_t i = 0; i < widths.size(); ++i) {
    const int gap = (i == row_first) ? 0 : m.item_gap;
    if (i != row_first && row_w + gap + widths[i] > inner) {
      row_widths.push_back(row_w);
      row_start.push_back(row_first);
      row_first = i;
      row_w = widths[i];
    } else {
      row_w += gap + widths[i];
    }
  }
  if (!widths.empty()) {
    row_widths.push_back(row_w);
    row_start.push_back(row_first);
  }

  int ped_min = m.item_pad_x * 2 + 1;
  if (ped_min > inner) ped_min = inner;
  const int ped_w =
      has_preedit
          ? Clamp(m.item_pad_x * 2 + (preedit_width > 0 ? preedit_width : 0),
                  ped_min, inner)
          : 0;
  int content_w = ped_w;
  for (int w : row_widths) {
    if (w > content_w) content_w = w;
  }
  if (content_w < 1) content_w = 1;

  out.width = content_w + frame_x;
  const int rows = static_cast<int>(row_widths.size());
  out.row_count = rows;
  const int preedit_block = has_preedit ? m.row_h + m.preedit_gap : 0;
  out.height = base_y * 2 + preedit_block + rows * m.row_h;

  const int content_left = m.border + m.pad_x;
  if (has_preedit) {
    out.preedit.left = content_left;
    out.preedit.top = base_y;
    out.preedit.right = content_left + content_w;
    out.preedit.bottom = base_y + m.row_h;
  }

  const int rows_top = base_y + preedit_block;
  out.items.resize(widths.size());
  for (int r = 0; r < rows; ++r) {
    const int y = rows_top + r * m.row_h;
    int x = content_left;
    const size_t first = row_start[static_cast<size_t>(r)];
    const size_t count =
        (static_cast<size_t>(r) + 1 < row_start.size())
            ? row_start[static_cast<size_t>(r) + 1] - first
            : widths.size() - first;
    for (size_t k = 0; k < count; ++k) {
      const size_t i = first + k;
      RECT rect = {x, y, x + widths[i], y + m.row_h};
      out.items[i] = rect;
      x += widths[i] + m.item_gap;
    }
  }
  return out;
}

int HitTestCandidate(const CandidateLayout& layout, POINT pt) {
  for (size_t i = 0; i < layout.items.size(); ++i) {
    const RECT& rect = layout.items[i];
    if (pt.x >= rect.left && pt.x < rect.right && pt.y >= rect.top &&
        pt.y < rect.bottom) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int CandidateDigitIndex(int keysym, size_t candidate_count) {
  if (keysym < '1' || keysym > '9') return -1;
  const int index = keysym - '1';
  if (index >= static_cast<int>(candidate_count)) return -1;
  return index;
}

bool AcceptCandidateClick(uint64_t click_version, uint64_t current_version,
                          int page_index, size_t candidate_count) {
  if (click_version == 0 || click_version != current_version) return false;
  if (page_index < 0) return false;
  return static_cast<size_t>(page_index) < candidate_count;
}
