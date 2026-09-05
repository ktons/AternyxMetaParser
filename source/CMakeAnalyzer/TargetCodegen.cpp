#include "CMakeAnalyzer/TargetCodegen.h"

#include <filesystem>
#include <stdexcept>
#include <unordered_map>

#include "CMakeAnalyzer/HeaderScanner.h"
#include "CodeGenerator/CodeGenerator.h"
#include "Parser/Parser.h"
#include "Utils/ProcessUtil.h"
#include "Utils/Utils.h"

namespace fs = std::filesystem;

namespace Aternyx::CMake {
namespace {

// The include roots the target compiles with: its own paths plus the extra
// ones. Used both as clang parse inputs and as the spelling roots for the
// generated `#include` lines.
std::vector<std::string> TargetIncludePaths(const CMakeTarget& target, const TargetCodegenOptions& options) {
  std::vector<std::string> includePaths = target.includePaths;
  includePaths.insert(includePaths.end(), options.extraIncludePaths.begin(), options.extraIncludePaths.end());
  return includePaths;
}

// Applies the per-template `post_process` shell filters from the registry:
// the configuration-side equivalent of a transformOutput hook. Runs after
// the caller's transformOutput, so a filter sees the final content. A hook
// that skips an output also skips its filter.
void ApplyConfiguredPostProcess(const TargetCodegenOptions& options, Aternyx::CodegenHooks& hooks) {
  std::unordered_map<std::string, std::string> commands;  // lowercased template name -> filter command
  for (const Aternyx::TemplateDesc& desc : options.templates) {
    if (!desc.postProcess.empty())
      commands[Aternyx::StringLib::ToLower(desc.name)] = desc.postProcess;
  }
  if (commands.empty())
    return;

  auto userHook = std::move(hooks.transformOutput);
  hooks.transformOutput = [userHook = std::move(userHook), commands = std::move(commands)](
                              const Aternyx::GenJob& job, std::string content) -> std::optional<std::string> {
    if (userHook) {
      std::optional<std::string> transformed = userHook(job, std::move(content));
      if (!transformed)
        return std::nullopt;
      content = std::move(*transformed);
    }
    if (auto it = commands.find(job.templateName); it != commands.end())
      return ProcessUtil::RunFilter(it->second, job.outputName, content);
    return content;
  };
}

}  // namespace

TargetParseResult ParseTargetAst(const CMakeTarget& target, const TargetCodegenOptions& options) {
  TargetParseResult result;

  const std::vector<std::string> includePaths = TargetIncludePaths(target, options);

  std::vector<std::string> extraClangArgs = target.defines;
  if (!target.cxxStandard.empty())
    extraClangArgs.push_back(target.cxxStandard);

  // Parse inputs: the target's .cpp files, or -- in parseHeaders mode -- the
  // annotated headers found under the source directories and the project
  // root. Parsing headers keeps generation decoupled from the .cpp files,
  // whose includes may reference generated outputs that do not exist yet on
  // the first run.
  std::vector<std::string> sources;
  if (options.parseHeaders) {
    std::vector<std::string> scanRoots;
    for (const std::string& sourceFile : target.cppFiles)
      scanRoots.push_back(fs::path{sourceFile}.parent_path().string());
    if (!options.projectPath.empty())
      scanRoots.push_back(options.projectPath);
    std::vector<std::string> markers = options.headerMarkers;
    if (markers.empty())
      markers = DefaultHeaderMarkers();
    sources = HeaderScanner::Scan(scanRoots, markers);
    if (sources.empty()) {
      throw std::runtime_error("no annotated headers found for target '" + target.name +
                               "' (scanned " + (scanRoots.empty() ? "nowhere" : "the configured roots") +
                               " for markers like \"CLASS(\"; check the header_markers configuration)");
    }
  } else {
    if (target.cppFiles.empty())
      throw std::runtime_error("Target '" + target.name + "' has no C++ source files to parse");
    sources = target.cppFiles;
  }

  for (const std::string& sourceFile : sources) {
    MetaParser parser{sourceFile, includePaths, extraClangArgs};
    try {
      parser.BuildCursor();
    } catch (const Aternyx::MetaParseError& e) {
      if (options.parseHeaders) {
        throw std::runtime_error("parsing header '" + sourceFile +
                                 "' failed (with --parse-headers, headers must be self-contained and must "
                                 "not include generated files): " +
                                 e.what());
      }
      throw;
    }
    result.tree.MergeFrom(std::move(parser.GetAstTree()));
    // Normalize the key the same way the parser normalizes the inclusion
    // paths, so CodeGenerator lookups match regardless of spelling.
    result.sourceIncludes[StringLib::NormalizePath(sourceFile)] = parser.GetIncludedFiles();
  }

  return result;
}

void RunTargetCodegen(const CMakeTarget& target, const TargetCodegenOptions& options) {
  RunTargetCodegen(target, options, Aternyx::CodegenHooks{});
}

void RunTargetCodegen(const CMakeTarget& target, const TargetCodegenOptions& options,
                      const Aternyx::CodegenHooks& hooks) {
  Aternyx::CodegenHooks effectiveHooks = hooks;
  ApplyConfiguredPostProcess(options, effectiveHooks);

  TargetParseResult parsed = ParseTargetAst(target, options);

  CodegenConfig config;
  config.outputPath = options.outputPath;
  config.templatePath = options.templatePath;
  config.pathStyle = options.pathStyle;
  // The spelling of the generated `#include` lines is derived ONLY from the
  // include roots the target compiles with, so generated files resolve their
  // includes by construction when compiled as part of this target. The
  // project root deliberately does not participate: headers outside every
  // target include root then fail loudly instead of producing includes that
  // the consumer cannot resolve.
  config.includeRoots = TargetIncludePaths(target, options);
  config.sourceIncludes = std::move(parsed.sourceIncludes);
  config.preludes = options.preludes;
  config.categories = options.categories;
  config.templates = options.templates;
  config.aggregators = options.aggregators;

  CodeGenerator generator;
  generator.Init(config, std::move(effectiveHooks));
  generator.SetAstTree(&parsed.tree);
  generator.Run();
}

}  // namespace Aternyx::CMake
