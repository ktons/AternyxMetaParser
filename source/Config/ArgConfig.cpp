#include "Config/ArgConfig.h"

#include <argparse.hpp>
#include <iostream>
#include <toml.hpp>

template <typename T>
inline void ApplyParserValue(T& value, const argparse::ArgumentParser& parser, const std::string& paramsName) {
  auto params = parser.get<T>(paramsName);
  if (!params.empty())
    value = params;
}

template <typename T>
inline void ApplyParserValue(std::vector<T>& value,
                             const argparse::ArgumentParser& parser,
                             const std::string& paramsName) {
  auto params = parser.get<std::vector<T>>(paramsName);
  for (auto& elem : params) {
    value.emplace_back(elem);
  }
}

bool ArgConfig::Validate() {
  return true;
}

void ArgConfig::DebugInfo() {
  std::cout << "output path:" << outputPath_ << std::endl;
}

bool ArgConfig::ParseArgs(int argc, char* argv[]) {
  const std::string progName = (argc > 0 && argv[0]) ? argv[0] : "AternyxParser";
  argparse::ArgumentParser parser(progName);

  parser.add_argument("source_file").help("Source file to parse").nargs(1).store_into(sourceFile_);

  parser.add_argument("-o", "--output-path").help("Generator output path").default_value(std::string("_generated"));

  parser.add_argument("-p", "--project-path")
      .help("Project root path, used when generate file which need include other file")
      .default_value("");

  parser.add_argument("-t", "--template-path").help("Template directory path").default_value(std::string("template"));

  parser.add_argument("-i", "--include-path").help("Include paths").nargs(argparse::nargs_pattern::any);

  parser.add_argument("--toml").help("Toml config file path").default_value(std::string{""});

  try {
    parser.parse_args(argc, const_cast<const char* const*>(argv));
  } catch (const std::runtime_error& err) {
    std::cerr << "Error parsing arguments: " << err.what() << std::endl;
    return false;
  }

  if (sourceFile_.empty()) {
    std::cerr << "No source file provided." << std::endl;
    return false;
  }

  auto tomlPath = parser.get<std::string>("--toml");
  if (!tomlPath.empty())
    ParseTomlConfig(tomlPath.data());

  ApplyParserValue(outputPath_, parser, "-o");
  ApplyParserValue(templatePath_, parser, "-t");
  ApplyParserValue(projectPath_, parser, "-p");
  ApplyParserValue(includePaths_, parser, "-i");

  return true;
}

bool ArgConfig::ParseTomlConfig(const char* tomlConfigPath) {
  if (tomlConfigPath == nullptr || *tomlConfigPath == '\0') {
    std::cerr << "Empty TOML config path provided." << std::endl;
    return false;
  }

  try {
    auto res = toml::parse_file(tomlConfigPath);

    toml::table tbl = res;

    if (auto v = tbl["output_path"].value<std::string>())
      outputPath_ = *v;

    if (auto v = tbl["project_path"].value<std::string>())
      projectPath_ = *v;

    if (auto v = tbl["template_path"].value<std::string>())
      templatePath_ = *v;

    if (auto arr = tbl["include_paths"].as_array()) {
      includePaths_.clear();
      for (auto& elem : *arr) {
        if (auto s = elem.value<std::string>())
          includePaths_.push_back(*s);
      }
    }

    return true;
  } catch (const std::exception& e) {
    std::cerr << "Exception while parsing TOML: " << e.what() << std::endl;
    return false;
  }
}
