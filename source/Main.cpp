// reference: https://github.com/AustinBrunkhorst/CPP-Reflection

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "CMakeAnalyzer/CMakeTargetAnalyzer.h"
#include "CMakeAnalyzer/TargetCodegen.h"
#include "Config/ArgConfig.h"

namespace fs = std::filesystem;

namespace {

Aternyx::CMake::TargetCodegenOptions MakeTargetCodegenOptionsFromArgs() {
  const ArgConfig& args = ArgConfig::Instance();
  Aternyx::CMake::TargetCodegenOptions options;
  options.outputPath = args.outputPath_;
  options.templatePath = args.templatePath_;
  options.projectPath = args.projectPath_;
  options.pathStyle = args.genPathStyle_;
  options.extraIncludePaths = args.includePaths_;
  options.parseHeaders = args.parseHeaders_;
  options.headerMarkers = args.headerMarkers_;
  return options;
}

// Analyze the compile database; with --target, generate code for that
// target, otherwise write a per-target report.
int RunCMakeMode() {
  const ArgConfig& args = ArgConfig::Instance();

  Aternyx::CMake::CMakeTargetAnalyzer analyzer;
  if (!analyzer.Analyze(args.compileDbPath_))
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
    report["compile_commands"] = args.compileDbPath_;
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
    return RunCMakeMode();
  } catch (const std::exception& e) {
    std::cerr << "[AternyxParser] error: " << e.what() << std::endl;
    return 1;
  }
}
