#pragma once
#include <filesystem>
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

// Resolves `path` against the current working directory when relative, then
// lexically normalizes it (collapsing ".." components, trimming trailing
// separators). The result uses forward slashes.
std::string NormalizePath(const std::string& path);

// Spelling of the `#include` line that a file written into `referencingDir`
// should use to include `sourceFile`:
//   1. when one of `roots` contains `sourceFile`, the deepest (most specific)
//      such root wins and the spelling is the path relative to it — correct
//      by construction when the generated file is compiled by a target whose
//      compile command carries those same roots as include directories;
//   2. otherwise the spelling falls back to a path relative to
//      `referencingDir` itself, which resolves through the compiler's
//      quoted-include "directory of the including file first" rule.
// All inputs may be relative (resolved against the current working directory)
// and use either separator; the result always uses forward slashes.
// Throws std::runtime_error when no usable spelling can be produced (e.g.
// unrelated roots such as different Windows drives), never returns an empty
// string — an empty `#include ""` is always a bug.
std::string MakeIncludeSpelling(const std::string& sourceFile,
                                const std::string& referencingDir,
                                const std::vector<std::string>& roots);

// Case-insensitive equality
bool EqualsNoCase(const std::string& lhs, const std::string& rhs);

std::string ToLower(const std::string& input);

// Strip leading and trailing whitespace
std::string Trim(const std::string& input);
}  // namespace StringLib
}  // namespace Aternyx
