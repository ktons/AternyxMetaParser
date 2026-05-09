#include "Utils/Utils.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace Aternyx::StringLib {

std::vector<std::string> Split(const std::string& input, char delimiter) {
  std::vector<std::string> result;
  if (input.empty()) {
    result.push_back("");
    return result;
  }
  std::stringstream ss(input);
  std::string item;
  while (std::getline(ss, item, delimiter)) {
    result.push_back(item);
  }
  if (!input.empty() && input.back() == delimiter) {
    result.push_back("");
  }
  return result;
}

std::vector<std::string> Split(const std::string& input, const std::string& delimiter) {
  std::vector<std::string> result;
  if (input.empty()) {
    result.push_back("");
    return result;
  }
  std::string token;
  size_t pos = 0;
  size_t prev = 0;
  while ((pos = input.find(delimiter, prev)) != std::string::npos) {
    token = input.substr(prev, pos - prev);
    result.push_back(token);
    prev = pos + delimiter.length();
  }
  result.push_back(input.substr(prev));
  return result;
}

std::string GetUnixPath(const std::string& path) {
  std::string result = path;
  std::replace(result.begin(), result.end(), '\\', '/');
  return result;
}

std::string GetRelativePath(const std::string& path, const std::string& rootPath) {
  fs::path p(path);
  fs::path r(rootPath);
  return fs::relative(p, r).string();
}
}  // namespace Aternyx::StringLib
