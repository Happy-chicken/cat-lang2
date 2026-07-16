#include "file.h"

namespace cat {
void Location::print(ostream &os) const {
  os << "Line: " << line << ", Column: " << column;
}

const Location File::lookup_loc(Span::BytePos pos) const {
  auto it = std::upper_bound(line_starts.begin(), line_starts.end(), pos);

  // 如果 pos 在第一个元素之前（几乎不可能），则置为 0；否则回退一行。
  size_t line_idx =
      (it == line_starts.begin()) ? 0 : (it - line_starts.begin() - 1);

  size_t col = pos - line_starts[line_idx];
  return {line_idx + 1, col};
}

optional<std::string_view> File::get_line(size_t line_1based) const {
  if (line_1based == 0)
    return std::nullopt;

  size_t idx = line_1based - 1;
  if (idx >= line_starts.size())
    return std::nullopt;

  size_t start = line_starts[idx];
  size_t end =
      (idx + 1 < line_starts.size()) ? line_starts[idx + 1] : content.size();
  std::string_view line(content.data() + start, end - start);

  // remove trailing newline characters
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
    line.remove_suffix(1);
  }

  return line;
}
} // namespace cat