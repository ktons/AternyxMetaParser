#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Aternyx::CMake {

// Resolves `path` against `baseDirectory` when relative, then normalizes it
// (collapsing ".." components and trimming trailing separators). The result
// uses forward slashes.
std::string NormalizePath(const std::filesystem::path& baseDirectory, const std::string& path);

// One entry of a CMake compile database (compile_commands.json).
struct CompileEntry {
  // Working directory of the compile command; relative paths inside the
  // entry are resolved against it.
  std::string directory;
  // The compiled source file (made absolute during Load).
  std::string file;
  // Object file path as declared by the entry ("output" field, or the -o/Fo
  // argument). Used to attribute the entry to its CMake target.
  std::string output;
  // Compiler invocation split into arguments: compiler path + flags.
  std::vector<std::string> arguments;
};

// Splits a raw command line into arguments using the Windows command-line
// rules (backslash runs before quotes, doubled quotes), which also handles
// plain POSIX-style quoting correctly for compiler command lines.
std::vector<std::string> SplitCommandLine(const std::string& command);

// Loads and parses a compile_commands.json file.
class CompileCommandDb {
 public:
  // `path` may point at the JSON file itself or at a directory containing
  // one (e.g. a CMake build directory). Response-file arguments (@file)
  // found in commands are expanded. Returns false when the database cannot
  // be found or parsed; LastError() then holds the reason.
  bool Load(const std::string& path);

  const std::vector<CompileEntry>& Entries() const { return entries_; }
  const std::string& LastError() const { return lastError_; }

 private:
  std::vector<CompileEntry> entries_;
  std::string lastError_;
};

}  // namespace Aternyx::CMake
