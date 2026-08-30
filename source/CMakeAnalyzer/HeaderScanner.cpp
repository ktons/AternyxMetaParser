#include "CMakeAnalyzer/HeaderScanner.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

#include "Utils/Utils.h"

namespace fs = std::filesystem;

namespace Aternyx::CMake {

std::vector<std::string> DefaultHeaderMarkers() {
  return {"CLASS(", "STRUCT(", "ENUM_CLASS("};
}

namespace {

bool IsHeaderFile(const fs::path& path) {
  const std::string ext = path.extension().string();
  return ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".hh";
}

bool ContainsAnyMarker(const fs::path& path, const std::vector<std::string>& markers) {
  std::ifstream ifs{path, std::ios::binary};
  if (!ifs)
    return false;
  std::stringstream buffer;
  buffer << ifs.rdbuf();
  const std::string text = buffer.str();
  return std::any_of(markers.begin(), markers.end(),
                     [&text](const std::string& marker) { return text.find(marker) != std::string::npos; });
}

}  // namespace

std::vector<std::string> HeaderScanner::Scan(const std::vector<std::string>& roots,
                                             const std::vector<std::string>& markers) {
  std::set<std::string> found;
  for (const std::string& root : roots) {
    if (root.empty())
      continue;
    const fs::path rootPath = StringLib::NormalizePath(root);
    std::error_code ec;
    if (!fs::is_directory(rootPath, ec)) {
      std::cerr << "[HeaderScanner] warning: scan root is not a directory, skipped: " << rootPath.string()
                << std::endl;
      continue;
    }
    for (fs::recursive_directory_iterator it{rootPath, fs::directory_options::skip_permission_denied, ec}, end;
         it != end; it.increment(ec)) {
      if (ec) {
        std::cerr << "[HeaderScanner] warning: cannot continue scan under " << it->path().string() << ": "
                  << ec.message() << std::endl;
        ec.clear();
        continue;
      }
      const fs::path& path = it->path();
      if (!IsHeaderFile(path) || !ContainsAnyMarker(path, markers))
        continue;
      found.insert(StringLib::GetUnixPath(path.lexically_normal().generic_string()));
    }
  }
  return {found.begin(), found.end()};
}

}  // namespace Aternyx::CMake
