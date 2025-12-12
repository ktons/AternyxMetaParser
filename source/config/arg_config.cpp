#include "arg_config.h"

#include <argparse.hpp>
#include <iostream>
#include <toml.hpp>

template <typename T>
inline void ApplyParserValue(T& value, const argparse::ArgumentParser& parser, const std::string& params_name) {
  auto params = parser.get<T>(params_name);
  if (!params.empty())
    value = params;
}

template <typename T>
inline void ApplyParserValue(std::vector<T>& value,
                             const argparse::ArgumentParser& parser,
                             const std::string& params_name) {
  auto params = parser.get<std::vector<T>>(params_name);
  for (auto& elem : params) {
    value.emplace_back(elem);
  }
}

bool ArgConfig::Validate() {
  // return source_file != "" && project_path != "" &&
  return true;
}

void ArgConfig::DebugInfo() {
  std::cout << "output path:" << output_path << std::endl;
}

bool ArgConfig::ParseArgs(int argc, char* argv[]) {
  // Use the program name from argv[0] if available
  const std::string prog_name = (argc > 0 && argv[0]) ? argv[0] : "AternyxParser";
  argparse::ArgumentParser parser(prog_name);

  // Positional: source file to parse
  parser.add_argument("source_file").help("Source file to parse").nargs(1).store_into(source_file);

  // Optional named arguments
  parser.add_argument("-o", "--output-path").help("Generator output path").default_value(std::string("_generated"));

  parser.add_argument("-p", "--project-path")
      .help("Project root path, used when generate file which need include other file")
      .default_value("");

  parser.add_argument("-t", "--template-path").help("Template directory path").default_value(std::string("template"));

  parser.add_argument("-i", "--include-path").help("Include paths").nargs(argparse::nargs_pattern::any);

  // config file path.
  parser.add_argument("--toml").help("Toml config file path");

  try {
    parser.parse_args(argc, const_cast<const char* const*>(argv));
  } catch (const std::runtime_error& err) {
    // argparse prints a helpful message; also output the caught exception
    std::cerr << "Error parsing arguments: " << err.what() << std::endl;
    return false;
  }

  // Basic sanity check: ensure positional source_file was provided
  if (source_file.empty()) {
    std::cerr << "No source file provided." << std::endl;
    return false;
  }

  auto toml_path = parser.get<std::string>("--toml");
  if (!toml_path.empty())
    ParseTomlConfig(toml_path.data());

  ApplyParserValue(output_path, parser, "-o");
  ApplyParserValue(template_path, parser, "-t");
  ApplyParserValue(project_path, parser, "-p");
  ApplyParserValue(include_paths, parser, "-i");

  return true;
}

bool ArgConfig::ParseTomlConfig(const char* toml_config_path) {
  if (toml_config_path == nullptr || *toml_config_path == '\0') {
    std::cerr << "Empty TOML config path provided." << std::endl;
    return false;
  }

  try {
    auto res = toml::parse_file(toml_config_path);

    // `res` can be treated as a table
    toml::table tbl = res;

    if (auto v = tbl["output_path"].value<std::string>())
      output_path = *v;

    if (auto v = tbl["project_path"].value<std::string>())
      project_path = *v;

    if (auto v = tbl["template_path"].value<std::string>())
      template_path = *v;

    // include_paths as an array of strings
    if (auto arr = tbl["include_paths"].as_array()) {
      include_paths.clear();
      for (auto& elem : *arr) {
        if (auto s = elem.value<std::string>())
          include_paths.push_back(*s);
      }
    }

    return true;
  } catch (const std::exception& e) {
    std::cerr << "Exception while parsing TOML: " << e.what() << std::endl;
    return false;
  }
}