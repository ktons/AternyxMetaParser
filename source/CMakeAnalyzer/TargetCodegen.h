#pragma once

#include <string>
#include <vector>

#include "CMakeAnalyzer/CMakeTargetAnalyzer.h"
#include "Config/GenPathStyle.h"

namespace Aternyx::CMake {

struct TargetCodegenOptions {
  // Root directory all generated files are written into.
  std::string outputPath;
  // Directory holding the mustache templates.
  std::string templatePath;
  // Project root used to make generated `#include` lines relative.
  std::string projectPath;
  // Naming style of the generated sub-directories.
  GenPathStyle pathStyle{GenPathStyle::SnakeCase};
  // Additional include paths appended to the target's own ones (e.g. the
  // project root, so generated `#include "Runtime/..."` lines resolve).
  std::vector<std::string> extraIncludePaths;
};

// Runs reflection code generation for one CMake target: every .cpp of the
// target is parsed with the target's own include paths / defines / language
// standard, the collected ASTs are merged (headers shared between
// translation units are collected once) and a single CodeGenerator pass
// writes the generated files into `options.outputPath`.
//
// Throws on the first failure (fail-fast):
//  - MetaParseError when clang reports errors while parsing any source,
//  - std::runtime_error for empty targets or unusable configuration.
void RunTargetCodegen(const CMakeTarget& target, const TargetCodegenOptions& options);

}  // namespace Aternyx::CMake
