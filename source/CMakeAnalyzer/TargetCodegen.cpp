#include "CMakeAnalyzer/TargetCodegen.h"

#include <stdexcept>

#include "CodeGenerator/CodeGenerator.h"
#include "Parser/Parser.h"

namespace Aternyx::CMake {

void RunTargetCodegen(const CMakeTarget& target, const TargetCodegenOptions& options) {
  if (target.cppFiles.empty())
    throw std::runtime_error("Target '" + target.name + "' has no C++ source files to parse");

  std::vector<std::string> includePaths = target.includePaths;
  includePaths.insert(includePaths.end(), options.extraIncludePaths.begin(), options.extraIncludePaths.end());

  std::vector<std::string> extraClangArgs = target.defines;
  if (!target.cxxStandard.empty())
    extraClangArgs.push_back(target.cxxStandard);

  AstTree mergedTree;
  for (const std::string& sourceFile : target.cppFiles) {
    MetaParser parser{sourceFile, includePaths, extraClangArgs};
    parser.BuildCursor();
    mergedTree.MergeFrom(std::move(parser.GetAstTree()));
  }

  CodegenConfig config;
  config.outputPath = options.outputPath;
  config.templatePath = options.templatePath;
  config.projectPath = options.projectPath;
  config.pathStyle = options.pathStyle;

  CodeGenerator generator;
  generator.Init(config);
  generator.SetAstTree(&mergedTree);
  generator.Run();
}

}  // namespace Aternyx::CMake
