#pragma once

#include <string>
#include <vector>

namespace Aternyx::CMake {

// Textual markers identifying headers that may hold annotated types; a
// header is only parsed when its text contains one of these.
std::vector<std::string> DefaultHeaderMarkers();

// Collects candidate headers for --parse-headers mode: every header under
// `roots` (scanned recursively) whose text contains one of `markers`. The
// scan is purely textual — no clang, no parsing — so it also works before
// any generated file exists and never fails on broken code.
class HeaderScanner {
 public:
  // Returns absolute normalized header paths, sorted and de-duplicated.
  // Unreadable files or directories are reported on stderr and skipped.
  static std::vector<std::string> Scan(const std::vector<std::string>& roots,
                                       const std::vector<std::string>& markers);
};

}  // namespace Aternyx::CMake
