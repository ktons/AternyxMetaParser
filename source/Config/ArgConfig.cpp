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
  if (compileDbPath_.empty()) {
    std::cerr << "No compile_commands.json path provided." << std::endl;
    return false;
  }
  return true;
}

void ArgConfig::DebugInfo() {
  std::cout << "output path: " << outputPath_ << std::endl;
  std::cout << "gen path style: "
            << (genPathStyle_ == Aternyx::GenPathStyle::CamelCase ? "camel_case" : "snake_case")
            << std::endl;
  std::cout << "target: " << (target_.empty() ? "<report only>" : target_) << std::endl;
}

bool ArgConfig::ParseArgs(int argc, char* argv[]) {
  const std::string progName = (argc > 0 && argv[0]) ? argv[0] : "AternyxParser";
  argparse::ArgumentParser parser(progName);

  parser.add_argument("input")
      .help("compile_commands.json path (or its directory)")
      .nargs(1)
      .store_into(compileDbPath_);

  parser.add_argument("-o", "--output-path")
      .help("Generator output path")
      .default_value(std::string("_generated"));

  parser.add_argument("-p", "--project-path")
      .help("Extra header scan root for parse_headers mode")
      .default_value(std::string(""));

  parser.add_argument("-t", "--template-path")
      .help("Template directory path")
      .default_value(std::string("template"));

  parser.add_argument("-i", "--include-path").help("Include paths").nargs(argparse::nargs_pattern::any);

  parser.add_argument("--toml")
      .help("TOML config file path (gen_path_style / parse_headers / header_markers)")
      .default_value(std::string{""});

  parser.add_argument("--target")
      .help("run code generation for this target instead of only reporting")
      .default_value(std::string{""});

  try {
    parser.parse_args(argc, const_cast<const char* const*>(argv));
  } catch (const std::runtime_error& err) {
    std::cerr << "Error parsing arguments: " << err.what() << std::endl;
    return false;
  }

  // TOML values are applied first, then only explicitly given CLI options
  // overwrite them (TOML still wins over built-in defaults). A broken config
  // file aborts instead of silently running on defaults.
  auto tomlPath = parser.get<std::string>("--toml");
  if (!tomlPath.empty() && !ParseTomlConfig(tomlPath.data()))
    return false;

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

    if (auto node = tbl.get("gen_path_style"))
      if (auto v = node->value<std::string>()) {
        Aternyx::GenPathStyle style = Aternyx::GenPathStyle::SnakeCase;
        if (!Aternyx::ParseGenPathStyle(*v, style)) {
          std::cerr << "Unknown gen_path_style in TOML: " << *v << std::endl;
          return false;
        }
        genPathStyle_ = style;
      }

    if (auto node = tbl.get("parse_headers"))
      if (auto v = node->value<bool>())
        parseHeaders_ = *v;

    if (auto node = tbl.get("header_markers"))
      if (auto arr = node->as_array()) {
        headerMarkers_.clear();
        for (auto& elem : *arr) {
          if (auto s = elem.value<std::string>())
            headerMarkers_.push_back(*s);
        }
      }

    return true;
  } catch (const std::exception& e) {
    std::cerr << "Exception while parsing TOML: " << e.what() << std::endl;
    return false;
  }
}
