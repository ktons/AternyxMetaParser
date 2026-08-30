#include "Utils/Utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

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

std::string NormalizePath(const std::string& path) {
  fs::path candidate{path};
  if (candidate.is_relative())
    candidate = fs::current_path() / candidate;
  std::string normalized = candidate.lexically_normal().generic_string();
  // lexically_normal keeps a trailing separator when the last element is
  // ".." (e.g. "a/b/.." -> "a/"); drop it for stable comparisons.
  if (normalized.size() > 1 && normalized.back() == '/')
    normalized.pop_back();
  return normalized;
}

namespace {

// Path of `source` relative to `base`, or an empty optional when the two are
// unrelated (different root names / drives) — lexically_relative returns an
// empty path in that case, which must never leak into an #include line.
// With allowDotDot=false the relative path must not escape `base`, i.e.
// `base` actually contains `source`.
std::optional<std::string> RelativeSpelling(const fs::path& source, const fs::path& base, bool allowDotDot) {
  fs::path rel = source.lexically_relative(base);
  if (rel.empty())
    return std::nullopt;
  std::string spelling = GetUnixPath(rel.generic_string());
  if (spelling.empty() || spelling == ".")
    return std::nullopt;
  if (!allowDotDot && (spelling == ".." || spelling.rfind("../", 0) == 0))
    return std::nullopt;
  return spelling;
}

fs::path AbsoluteNormalized(const std::string& path) {
  fs::path candidate{path};
  if (candidate.is_relative())
    candidate = fs::current_path() / candidate;
  return candidate.lexically_normal();
}

}  // namespace

std::string MakeIncludeSpelling(const std::string& sourceFile,
                                const std::string& referencingDir,
                                const std::vector<std::string>& roots) {
  const fs::path source = AbsoluteNormalized(sourceFile);
  const fs::path referencing = AbsoluteNormalized(referencingDir);

  // The deepest containing root wins: the one sharing the longest prefix with
  // the source file, which yields the most specific (shortest) spelling.
  std::optional<std::string> best;
  size_t bestDepth = 0;
  for (const auto& root : roots) {
    if (root.empty())
      continue;
    const fs::path rootPath = AbsoluteNormalized(root);
    auto spelling = RelativeSpelling(source, rootPath, /*allowDotDot=*/false);
    if (!spelling)
      continue;
    const size_t depth = std::distance(rootPath.begin(), rootPath.end());
    if (!best || depth > bestDepth) {
      best = std::move(*spelling);
      bestDepth = depth;
    }
  }
  if (best)
    return *best;

  // No root contains the source file: fall back to a path relative to the
  // referencing file's own directory (compilers search the directory of the
  // including file first for quoted includes).
  auto fallback = RelativeSpelling(source, referencing, /*allowDotDot=*/true);
  if (!fallback) {
    throw std::runtime_error("cannot make an #include spelling for '" + GetUnixPath(sourceFile) +
                             "' referenced from '" + GetUnixPath(referencingDir) +
                             "': the paths are unrelated (different drives?). Add an include root "
                             "that contains the source file.");
  }
  return *fallback;
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
