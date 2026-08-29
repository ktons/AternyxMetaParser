#include "Utils/Utils.h"

#include <algorithm>
#include <cctype>
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

bool EqualsNoCase(const std::string& lhs, const std::string& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(lhs[i])) != std::tolower(static_cast<unsigned char>(rhs[i]))) {
      return false;
    }
  }
  return true;
}

std::string ToLower(const std::string& input) {
  std::string result = input;
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
  return result;
}

std::string Trim(const std::string& input) {
  size_t begin = 0;
  size_t end = input.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(input[begin]))) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
    --end;
  }
  return input.substr(begin, end - begin);
}
}  // namespace Aternyx::StringLib
