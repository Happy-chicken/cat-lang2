#pragma once
#include "common.h"
#include "span.h"
#include <fstream>
#include <string_view>
namespace cat {
struct Location {
  size_t line;
  size_t column;
  Location(size_t line, size_t column) : line(line), column(column) {}
  void print(ostream &os) const;
};
class File {
public:
  File(const string &name, const string &content)
      : file_name(name), content(content) {
    // file.open(file_name);
    line_starts.reserve(content.size() / 80 + 1);
    line_starts.push_back(0);
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == '\n') {
        line_starts.push_back(i + 1); // next position after '\n'
      }
    }
  }
  const Location lookup_loc(Span::BytePos pos) const;
  optional<std::string_view> get_line(size_t line_number) const;
  const string &name() const { return file_name; }

private:
  // TODO: add file stream
  // std::ifstream file;
  string file_name;
  string content;
  vector<size_t> line_starts;
};
} // namespace cat