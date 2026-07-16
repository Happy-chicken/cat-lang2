#include "diag.h"

namespace cat::error {
  void DiagCtxt::print_diagnostic(std::ostream &os, const Diagnostic &diag) const {
    const char *level_str = [&] {
      switch (diag.level) {
        case Level::Error:
          return "error";
        case Level::Warning:
          return "warning";
        case Level::Note:
          return "note";
        default:
          return "";
      }
    }();

    if (!diag.primary_label) {
      os << level_str << ": " << diag.message << '\n';
      return;
    }

    const auto &primary = *diag.primary_label;
    if (files.empty()) {
      os << level_str << ": " << diag.message << '\n';
      return;
    }

    const auto &file = files.begin()->second;
    auto loc_lo = file.lookup_loc(primary.span.low);
    auto loc_hi = file.lookup_loc(primary.span.high);

    // 主消息
    os << level_str << ": " << diag.message << '\n';
    os << "  --> " << file.name() << ':' << loc_lo.line << ':'
       << (loc_lo.column + 1) << '\n';

    // 打印源码行
    if (auto line = file.get_line(loc_lo.line)) {
      os << "   |\n";
      os << "   | " << *line << '\n';

      size_t start = loc_lo.column;
      size_t end = (loc_lo.line == loc_hi.line) ? loc_hi.column : line->size();
      size_t len = (end > start) ? (end - start) : 0;
      os << "   | " << std::string(start, ' ') << std::string(len, '^') << '\n';
    }

    for (const auto &note: diag.notes) {
      os << "   = note: " << note << '\n';
    }
  }
}// namespace cat::error
