#include "CMakeAnalyzer/TargetCodegen.h"

#include <filesystem>
#include <stdexcept>

#include "CMakeAnalyzer/HeaderScanner.h"
#include "CodeGenerator/CodeGenerator.h"
#include "Parser/Parser.h"
#include "Utils/Utils.h"

namespace fs = std::filesystem;

namespace Aternyx::CMake {

void RunTargetCodegen(const CMakeTarget& target, const TargetCodegenOptions& options) {
  std::vector<std::string> includePaths = target.includePaths;
  includePaths.insert(includePaths.end(), options.extraIncludePaths.begin(), options.extraIncludePaths.end());

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

  AstTree mergedTree;
  std::unordered_map<std::string, std::vector<std::string>> sourceIncludes;
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
    mergedTree.MergeFrom(std::move(parser.GetAstTree()));
    // Normalize the key the same way the parser normalizes the inclusion
    // paths, so CodeGenerator lookups match regardless of spelling.
    sourceIncludes[StringLib::NormalizePath(sourceFile)] = parser.GetIncludedFiles();
  }

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
  config.includeRoots = includePaths;
  config.sourceIncludes = std::move(sourceIncludes);

  CodeGenerator generator;
  generator.Init(config);
  generator.SetAstTree(&mergedTree);
  generator.Run();
}

}  // namespace Aternyx::CMake
