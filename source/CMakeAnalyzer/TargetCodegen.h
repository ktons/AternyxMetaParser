#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "CMakeAnalyzer/CMakeTargetAnalyzer.h"
#include "CodeGenerator/CodeGenerator.h"
#include "Config/GenPathStyle.h"

namespace Aternyx::CMake {

struct TargetCodegenOptions {
  // Root directory all generated files are written into.
  std::string outputPath;
  // Directory holding the mustache templates.
  std::string templatePath;
  // Extra header scan root for parseHeaders mode. Deliberately NOT an
  // include-spelling root: `#include` spellings come only from the target's
  // own include roots, so a scan root that is too broad fails loudly instead
  // of emitting includes the consumer cannot resolve.
  std::string projectPath;
  // Naming style of the generated sub-directories.
  GenPathStyle pathStyle{GenPathStyle::SnakeCase};
  // Additional include paths appended to the target's own ones (e.g. the
  // project root, so generated `#include "Runtime/..."` lines resolve).
  std::vector<std::string> extraIncludePaths;
  // Parse annotated headers instead of the target's .cpp files
  // (.h-as-source). Headers are collected from the directories of the
  // target's sources and `projectPath`; they must be self-contained and must
  // not include generated files.
  bool parseHeaders{false};
  // Text markers identifying annotated headers (e.g. "CLASS("); empty uses
  // the built-in defaults.
  std::vector<std::string> headerMarkers;
  // Per-category prelude includes for all_include.gen.h (see CodegenConfig).
  std::unordered_map<Aternyx::TempType, std::vector<std::string>> preIncludes;
};

// Runs reflection code generation for one CMake target. The parse inputs are
// the target's .cpp files (or, with parseHeaders, its annotated headers);
// each is parsed with the target's own include paths / defines / language
// standard, the collected ASTs are merged (headers shared between
// translation units are collected once) and a single CodeGenerator pass
// writes the generated files into `options.outputPath`. The `#include` lines
// inside generated files are spelled against the target's include roots, so
// they resolve when the generated files are compiled by the same target.
//
// Throws on the first failure (fail-fast):
//  - MetaParseError when clang reports errors while parsing any source,
//  - std::runtime_error for empty targets, unusable configuration, or (in
//    parseHeaders mode) headers that do not compile standalone.
void RunTargetCodegen(const CMakeTarget& target, const TargetCodegenOptions& options);

}  // namespace Aternyx::CMake
