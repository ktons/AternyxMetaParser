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
  // Per-category prelude includes injected at the top of each category's
  // generated all_include.gen.h. Empty by default: the library itself is
  // engine-agnostic — the CLI installs the engine preludes (Main.cpp).
  std::unordered_map<std::string, std::vector<std::string>> preludes;
  // Optional codegen registry extensions (see CodeGenerator.h). Empty lists
  // keep the built-in defaults; non-empty lists replace them — mix the
  // Default*() entries back in to extend rather than replace.
  std::vector<Aternyx::CodegenCategory> categories;
  std::vector<Aternyx::TemplateDesc> templates;
  std::vector<Aternyx::CodegenAggregator> aggregators;
};

// The parse half of a target codegen run: everything up to (but excluding)
// the CodeGenerator. Upper layers that want full control over generation can
// call this instead of RunTargetCodegen and drive their own pipeline with
// the merged tree — the escape hatch alongside CodegenHooks.
struct TargetParseResult {
  // Merged AST of every parsed source: the annotated types of the whole
  // target (headers shared between translation units are collected once).
  Aternyx::AstTree tree;
  // Normalized source file -> every file it transitively #includes (the
  // data behind `gen_include_list`).
  std::unordered_map<std::string, std::vector<std::string>> sourceIncludes;
};

// Parses one target's sources for code generation. The parse inputs are the
// target's .cpp files (or, with parseHeaders, its annotated headers); each
// is parsed with the target's own include paths / defines / language
// standard and the collected ASTs are merged.
//
// Throws on the first failure (fail-fast):
//  - MetaParseError when clang reports errors while parsing any source,
//  - std::runtime_error for empty targets or, in parseHeaders mode, headers
//    that do not compile standalone.
TargetParseResult ParseTargetAst(const CMakeTarget& target, const TargetCodegenOptions& options);

// Two-argument overload kept for call sites without hooks.
void RunTargetCodegen(const CMakeTarget& target, const TargetCodegenOptions& options);

// Runs reflection code generation for one CMake target: ParseTargetAst, then
// a single CodeGenerator pass writing into `options.outputPath`, with
// `hooks` injected along the generation pipeline (see CodegenHooks). The
// `#include` lines inside generated files are spelled against the target's
// include roots, so they resolve when the generated files are compiled by
// the same target.
//
// Throws on the first failure (fail-fast): MetaParseError /
// std::runtime_error from the parse stage (see ParseTargetAst), registry
// errors from CodeGenerator::Init, or anything a hook throws.
void RunTargetCodegen(const CMakeTarget& target, const TargetCodegenOptions& options,
                      const Aternyx::CodegenHooks& hooks);

}  // namespace Aternyx::CMake
