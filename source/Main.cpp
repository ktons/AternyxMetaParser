// reference: https://github.com/AustinBrunkhorst/CPP-Reflection

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "CMakeAnalyzer/CMakeTargetAnalyzer.h"
#include "CMakeAnalyzer/TargetCodegen.h"
#include "Config/ArgConfig.h"
#include "Utils/Utils.h"

namespace fs = std::filesystem;

namespace {

// Engine-specific allinclude preludes, injected per generated category. The
// library ships none (CodegenConfig::preludes is empty by default): which
// headers the generated code assumes is this tool's concern, not the
// CodeGenerator's. Per-file generated headers assume the category aggregator
// (or an equivalent prelude) is included first.
const std::unordered_map<std::string, std::vector<std::string>>& EnginePreludes() {
  static const std::unordered_map<std::string, std::vector<std::string>> kEnginePreludes = {
      {
          std::string(Aternyx::kCategorySerialization),
          {"<yaml-cpp/yaml.h>", "\"precompile/core_serialization.h\"", "<ucore/struct/object_pool.h>",
           "\"precompile/component_serialization.h\""},
      },
      {
          std::string(Aternyx::kCategoryEditorUi),
          {"\"editor/editor_ui/utility/imgui_utility.h\"", "\"editor/editor_ui/utility/imgui_user_lib.h\""},
      },
      {
          std::string(Aternyx::kCategoryReflection),
          {"<ucore/struct/object_pool.h>"},
      },
  };
  return kEnginePreludes;
}

// The TOML registry is additive over the built-in defaults: a TOML entry
// with the same key replaces the built-in, a new key is appended, and the
// `remove = true` entries drop built-ins (see ParseTomlConfig). Categories
// come first: template validation requires every referenced category.
std::vector<Aternyx::CodegenCategory> MergeCategories(const ArgConfig& args) {
  std::vector<Aternyx::CodegenCategory> categories = Aternyx::DefaultCodegenCategories();
  for (auto& category : args.codegenCategories_) {
    auto it = std::find_if(categories.begin(), categories.end(),
                           [&](const Aternyx::CodegenCategory& c) { return c.name == category.name; });
    if (it != categories.end())
      *it = category;
    else
      categories.push_back(category);
  }
  return categories;
}

std::vector<Aternyx::TemplateDesc> MergeTemplates(const ArgConfig& args) {
  std::vector<Aternyx::TemplateDesc> templates = Aternyx::DefaultTemplates();
  auto matches = [&](const Aternyx::TemplateDesc& desc, const std::string& lowerName) {
    return Aternyx::StringLib::ToLower(desc.name) == lowerName;
  };
  for (const std::string& removed : args.removedTemplates_) {
    templates.erase(std::remove_if(templates.begin(), templates.end(),
                                   [&](const Aternyx::TemplateDesc& desc) { return matches(desc, removed); }),
                    templates.end());
  }
  for (auto& desc : args.codegenTemplates_) {
    auto it = std::find_if(templates.begin(), templates.end(),
                           [&](const Aternyx::TemplateDesc& entry) { return matches(entry, desc.name); });
    if (it != templates.end())
      *it = desc;
    else
      templates.push_back(desc);
  }
  return templates;
}

std::vector<Aternyx::CodegenAggregator> MergeAggregators(const ArgConfig& args) {
  std::vector<Aternyx::CodegenAggregator> aggregators = Aternyx::DefaultAggregators();
  for (const std::string& removed : args.removedAggregators_) {
    aggregators.erase(std::remove_if(aggregators.begin(), aggregators.end(),
                                     [&](const Aternyx::CodegenAggregator& a) { return a.category == removed; }),
                      aggregators.end());
  }
  for (auto& aggregator : args.codegenAggregators_) {
    auto it = std::find_if(aggregators.begin(), aggregators.end(),
                           [&](const Aternyx::CodegenAggregator& a) { return a.category == aggregator.category; });
    if (it != aggregators.end())
      *it = aggregator;
    else
      aggregators.push_back(aggregator);
  }
  return aggregators;
}

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
  options.preludes = EnginePreludes();
  // TOML preludes replace the engine defaults per configured category (an
  // explicit empty includes list clears it).
  for (auto& [category, includes] : args.codegenPreludes_)
    options.preludes[category] = includes;
  options.categories = MergeCategories(args);
  options.templates = MergeTemplates(args);
  options.aggregators = MergeAggregators(args);
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
