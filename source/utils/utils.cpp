#include "utils.h"

#include <algorithm>
#include <sstream>

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
  // 处理结尾分隔符导致的空项
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
  if (delimiter.empty()) {
    result.push_back(input);
    return result;
  }
  size_t start = 0, end;
  while ((end = input.find(delimiter, start)) != std::string::npos) {
    result.push_back(input.substr(start, end - start));
    start = end + delimiter.length();
  }
  result.push_back(input.substr(start));
  // 处理结尾分隔符导致的空项
  if (!input.empty() && input.size() >= delimiter.size() &&
      input.substr(input.size() - delimiter.size()) == delimiter) {
    result.push_back("");
  }
  return result;
}

std::string GetUnixPath(const std::string& path) {
  std::string unixPath = path;
  std::replace(unixPath.begin(), unixPath.end(), '\\', '/');
  return unixPath;
}

std::string GetRelativePath(const std::string& path, const std::string& rootPath) {
  std::string unixPath = GetUnixPath(path);
  std::string unixRoot = GetUnixPath(rootPath);
  if (!unixRoot.empty() && unixRoot.back() != '/') {
    unixRoot += '/';
  }
  if (unixPath.find(unixRoot) == 0) {
    return unixPath.substr(unixRoot.length());
  }
  return unixPath;
}
}  // namespace Aternyx::StringLib