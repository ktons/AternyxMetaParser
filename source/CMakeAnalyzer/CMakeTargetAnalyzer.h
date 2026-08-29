#pragma once

#include <string>
#include <vector>

#include "CMakeAnalyzer/CompileCommands.h"

namespace Aternyx::CMake {

// Compile information of one CMake target, gathered from the entries of a
// compile database. Include paths are applied as-is (no filtering): the
// database already reflects the exact flags the real build uses.
struct CMakeTarget {
  std::string name;
  // Absolute, normalized, de-duplicated source files (.cpp/.cc/.cxx/.c++).
  std::vector<std::string> cppFiles;
  // Absolute, normalized, de-duplicated include paths (first-seen order).
  std::vector<std::string> includePaths;
  // Clang-style defines ("-D..."), de-duplicated.
  std::vector<std::string> defines;
  // Language standard as a clang argument ("-std=c++17"), when declared.
  std::string cxxStandard;
};

// Groups the entries of a compile database into targets and extracts the
// information needed to parse each target's sources with libclang.
class CMakeTargetAnalyzer {
 public:
  // Loads `dbPath` (file or directory holding compile_commands.json) and
  // groups all entries. Returns false when the database cannot be loaded;
  // LastError() explains why. Entries whose target cannot be determined are
  // reported on stderr and skipped.
  bool Analyze(const std::string& dbPath);

  const std::vector<CMakeTarget>& Targets() const { return targets_; }
  const CMakeTarget* FindTarget(const std::string& name) const;
  const std::string& LastError() const { return lastError_; }

 private:
  std::vector<CMakeTarget> targets_;
  std::string lastError_;
};

}  // namespace Aternyx::CMake
