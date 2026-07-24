#pragma once

#include "common.h"
#include "file.h"
#include "span.h"

namespace cat::error {
class DiagCtxt;
enum class Level { Error, Warning, Note };

struct Label {
  Span span;
  std::optional<string> message;
};

struct Diagnostic {
  Level level;
  string message;
  std::optional<Label> primary_label;
  vector<string> notes;
};

class DiagnosticBuilder {
public:
  DiagnosticBuilder(Level level, string msg)
      : diag{level, std::move(msg), std::nullopt, {}} {}

  DiagnosticBuilder &&span(const Span &s) && {
    diag.primary_label = Label{s, std::nullopt};
    return std::move(*this);
  }

  DiagnosticBuilder &&span_with_message(const Span &s, string msg) && {
    diag.primary_label = Label{s, std::move(msg)};
    return std::move(*this);
  }

  DiagnosticBuilder &&note(string text) && {
    diag.notes.push_back(std::move(text));
    return std::move(*this);
  }

  Diagnostic build() && { return std::move(diag); }

  void emit_to(DiagCtxt &ctx) &&;

private:
  Diagnostic diag;
};
} // namespace cat::error

namespace cat::error {
class DiagCtxt {
public:
  DiagCtxt() = default;

  void add_file(File file) { files.emplace(file.name(), std::move(file)); }

  void emit(Diagnostic diag) {
    if (diag.level == Level::Error) {
      ++error_count;
    }
    diagnostics.push_back(std::move(diag));
  }

  // 快速创建错误诊断（返回 builder）
  DiagnosticBuilder error(const Span &span, string msg) {
    return DiagnosticBuilder(Level::Error, std::move(msg)).span(span);
  }

  DiagnosticBuilder warn(string msg) {
    return DiagnosticBuilder(Level::Warning, std::move(msg));
  }

  bool has_errors() const noexcept { return error_count > 0; }

  void print_all(std::ostream &os) const {
    for (const auto &diag : diagnostics) {
      print_diagnostic(os, diag);
    }
  }

private:
  std::unordered_map<string, File> files;
  vector<Diagnostic> diagnostics;
  size_t error_count = 0;

  void print_diagnostic(std::ostream &os, const Diagnostic &diag) const;
};

inline void DiagnosticBuilder::emit_to(DiagCtxt &ctx) && {
  ctx.emit(std::move(diag));
}

} // namespace cat::error
