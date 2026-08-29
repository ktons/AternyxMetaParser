// reference: https://github.com/AustinBrunkhorst/CPP-Reflection

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "CMakeAnalyzer/CMakeTargetAnalyzer.h"
#include "CMakeAnalyzer/TargetCodegen.h"
#include "CodeGenerator/CodeGenerator.h"
#include "Config/ArgConfig.h"
#include "Parser/Parser.h"

namespace fs = std::filesystem;

namespace {

Aternyx::CodegenConfig MakeCodegenConfigFromArgs() {
  const ArgConfig& args = ArgConfig::Instance();
  Aternyx::CodegenConfig config;
  config.outputPath = args.outputPath_;
  config.templatePath = args.templatePath_;
  config.projectPath = args.projectPath_;
  config.pathStyle = args.genPathStyle_;
  return config;
}

Aternyx::CMake::TargetCodegenOptions MakeTargetCodegenOptionsFromArgs() {
  const ArgConfig& args = ArgConfig::Instance();
  Aternyx::CMake::TargetCodegenOptions options;
  options.outputPath = args.outputPath_;
  options.templatePath = args.templatePath_;
  options.projectPath = args.projectPath_;
  options.pathStyle = args.genPathStyle_;
  options.extraIncludePaths = args.includePaths_;
  return options;
}

// Classic behavior: parse the input source file, generate code.
int RunCodegenMode() {
  ArgConfig& args = ArgConfig::Instance();

  Aternyx::MetaParser parser{args.sourceFile_, args.includePaths_};
  parser.BuildCursor();

  Aternyx::CodeGenerator generator;
  generator.Init(MakeCodegenConfigFromArgs());
  generator.SetAstTree(&parser.GetAstTree());
  generator.Run();
  return 0;
}

// cmake mode: analyze the compile database; with --target, generate code for
// that target, otherwise write a per-target report.
int RunCMakeMode() {
  const ArgConfig& args = ArgConfig::Instance();

  Aternyx::CMake::CMakeTargetAnalyzer analyzer;
  if (!analyzer.Analyze(args.sourceFile_))
    throw std::runtime_error(analyzer.LastError());

  if (args.target_.empty()) {
    for (const Aternyx::CMake::CMakeTarget& target : analyzer.Targets()) {
      std::cout << "target: " << target.name << "\n";
      std::cout << "  sources: " << target.cppFiles.size() << "\n";
      std::cout << "  include paths:\n";
      for (const std::string& includePath : target.includePaths)
        std::cout << "    " << includePath << "\n";
    }

    nlohmann::json report;
    report["compile_commands"] = args.sourceFile_;
    report["targets"] = nlohmann::json::array();
    for (const Aternyx::CMake::CMakeTarget& target : analyzer.Targets()) {
      nlohmann::json entry;
      entry["name"] = target.name;
      entry["source_files"] = target.cppFiles;
      entry["include_paths"] = target.includePaths;
      entry["defines"] = target.defines;
      entry["cxx_standard"] = target.cxxStandard;
      report["targets"].push_back(entry);
    }

    fs::create_directories(args.outputPath_);
    const fs::path reportPath = fs::path(args.outputPath_) / "cmake_targets_report.json";
    std::ofstream ofs(reportPath, std::ios::binary);
    ofs << report.dump(2) << std::endl;
    ofs.close();
    std::cout << "report written: " << reportPath.string() << std::endl;
    return 0;
  }

  const Aternyx::CMake::CMakeTarget* target = analyzer.FindTarget(args.target_);
  if (target == nullptr) {
    std::string available;
    for (const Aternyx::CMake::CMakeTarget& entry : analyzer.Targets()) {
      if (!available.empty())
        available += ", ";
      available += entry.name;
    }
    throw std::runtime_error("target '" + args.target_ + "' not found in the compile database. available targets: " +
                             available);
  }

  Aternyx::CMake::RunTargetCodegen(*target, MakeTargetCodegenOptionsFromArgs());
  std::cout << "code generation for target '" << target->name << "' finished, output: " << args.outputPath_
            << std::endl;
  return 0;
}

}  // namespace

// note: argv 0 is this exe
int main(int argc, char* argv[]) {
  if (!ArgConfig::Instance().ParseArgs(argc, argv))
    return -1;

  try {
    if (ArgConfig::Instance().mode == Aternyx::CliMode::CMake)
      return RunCMakeMode();
    return RunCodegenMode();
  } catch (const std::exception& e) {
    std::cerr << "[AternyxParser] error: " << e.what() << std::endl;
    return 1;
  }
}
