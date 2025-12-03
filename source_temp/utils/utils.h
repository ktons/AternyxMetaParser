#pragma once
#include <string>
#include <vector>

namespace Aternyx {
namespace StringLib {

// Split a string by single char delimiter
std::vector<std::string> Split(const std::string& input, char delimiter);
// Split a string by string delimiter
std::vector<std::string> Split(const std::string& input, const std::string& delimiter);

std::string GetUnixPath(const std::string& path);

std::string GetRelativePath(const std::string& path, const std::string& rootPath);
}  // namespace StringLib
}  // namespace Aternyx
