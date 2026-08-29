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
  if (sourceFile_.empty()) {
    std::cerr << (mode == Aternyx::CliMode::CMake
                      ? "No compile_commands.json path provided."
                      : "No source file provided.")
              << std::endl;
    return false;
  }
  return true;
}

void ArgConfig::DebugInfo() {
  std::cout << "mode: " << (mode == Aternyx::CliMode::CMake ? "cmake" : "codegen") << std::endl;
  std::cout << "output path: " << outputPath_ << std::endl;
  std::cout << "gen path style: "
            << (genPathStyle_ == Aternyx::GenPathStyle::CamelCase ? "camel_case" : "snake_case")
            << std::endl;
  if (mode == Aternyx::CliMode::CMake)
    std::cout << "target: " << (target_.empty() ? "<report only>" : target_) << std::endl;
}

bool ArgConfig::ParseArgs(int argc, char* argv[]) {
  const std::string progName = (argc > 0 && argv[0]) ? argv[0] : "AternyxParser";
  argparse::ArgumentParser parser(progName);

  parser.add_argument("input")
      .help("codegen mode: source file to parse; cmake mode: compile_commands.json path (or its directory)")
      .nargs(1)
      .store_into(sourceFile_);

  parser.add_argument("-o", "--output-path")
      .help("Generator output path")
      .default_value(std::string("_generated"));

  parser.add_argument("-p", "--project-path")
      .help("Project root path, used when generate file which need include other file")
      .default_value(std::string(""));

  parser.add_argument("-t", "--template-path")
      .help("Template directory path")
      .default_value(std::string("template"));

  parser.add_argument("-i", "--include-path").help("Include paths").nargs(argparse::nargs_pattern::any);

  parser.add_argument("--toml").help("Toml config file path").default_value(std::string{""});

  bool codegenFlag = false;
  bool cmakeFlag = false;
  auto& modeGroup = parser.add_mutually_exclusive_group();
  modeGroup.add_argument("--codegen")
      .help("codegen mode: analyze the input source file and generate code (default)")
      .flag()
      .store_into(codegenFlag);
  modeGroup.add_argument("--cmake")
      .help("cmake mode: analyze the compile database given as <input>; add --target to also generate code")
      .flag()
      .store_into(cmakeFlag);

  parser.add_argument("--target")
      .help("cmake mode: run code generation for this target instead of only reporting")
      .default_value(std::string{""});

  parser.add_argument("--gen-path-style")
      .help("Naming style of the generated sub-directories (serialization / editor_ui / reflection)")
      .choices("snake_case", "camel_case")
      .default_value(std::string{"snake_case"});

  try {
    parser.parse_args(argc, const_cast<const char* const*>(argv));
  } catch (const std::runtime_error& err) {
    std::cerr << "Error parsing arguments: " << err.what() << std::endl;
    return false;
  }

  mode = cmakeFlag ? Aternyx::CliMode::CMake : Aternyx::CliMode::Codegen;

  // TOML values are applied first, then only explicitly given CLI options
  // overwrite them (TOML still wins over built-in defaults).
  auto tomlPath = parser.get<std::string>("--toml");
  if (!tomlPath.empty())
    ParseTomlConfig(tomlPath.data());

  if (parser.is_used("-o"))
    outputPath_ = parser.get<std::string>("-o");
  if (parser.is_used("-t"))
    templatePath_ = parser.get<std::string>("-t");
  if (parser.is_used("-p"))
    projectPath_ = parser.get<std::string>("-p");
  if (parser.is_used("-i")) {
    includePaths_.clear();
    ApplyParserValue(includePaths_, parser, "-i");
  }

  target_ = parser.get<std::string>("--target");
  // choices() already restricted the value; parse it for the enum.
  Aternyx::ParseGenPathStyle(parser.get<std::string>("--gen-path-style"), genPathStyle_);

  return Validate();
}

bool ArgConfig::ParseTomlConfig(const char* tomlConfigPath) {
  if (tomlConfigPath == nullptr || *tomlConfigPath == '\0') {
    std::cerr << "Empty TOML config path provided." << std::endl;
    return false;
  }

  try {
    auto res = toml::parse_file(tomlConfigPath);
    toml::table tbl = res;

    // Keys may live at the file root or inside a [parserParams] table.
    const toml::table* paramsTable = tbl.get_as<toml::table>("parserParams");
    auto findNode = [&](const char* key) -> const toml::node* {
      if (auto node = tbl.get(key))
        return node;
      if (paramsTable != nullptr)
        if (auto node = paramsTable->get(key))
          return node;
      return nullptr;
    };

    if (auto node = findNode("output_path"))
      if (auto v = node->value<std::string>())
        outputPath_ = *v;

    if (auto node = findNode("project_path"))
      if (auto v = node->value<std::string>())
        projectPath_ = *v;

    if (auto node = findNode("template_path"))
      if (auto v = node->value<std::string>())
        templatePath_ = *v;

    if (auto node = findNode("include_paths"))
      if (auto arr = node->as_array()) {
        includePaths_.clear();
        for (auto& elem : *arr) {
          if (auto s = elem.value<std::string>())
            includePaths_.push_back(*s);
        }
      }

    if (auto node = findNode("target"))
      if (auto v = node->value<std::string>())
        target_ = *v;

    if (auto node = findNode("gen_path_style"))
      if (auto v = node->value<std::string>()) {
        Aternyx::GenPathStyle style = Aternyx::GenPathStyle::SnakeCase;
        if (!Aternyx::ParseGenPathStyle(*v, style)) {
          std::cerr << "Unknown gen_path_style in TOML: " << *v << std::endl;
          return false;
        }
        genPathStyle_ = style;
      }

    return true;
  } catch (const std::exception& e) {
    std::cerr << "Exception while parsing TOML: " << e.what() << std::endl;
    return false;
  }
}
