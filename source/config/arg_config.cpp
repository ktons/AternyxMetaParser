#include "arg_config.h"

#include <argparse.hpp>
#include <iostream>

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

  // Optional named arguments
  parser.add_argument("-o", "--output-path")
      .help("Generator output path")
      .default_value("_generated")
      .store_into(output_path);

  parser.add_argument("-p", "--project-path")
      .help("Project root path")
      .default_value(std::string(""))
      .store_into(project_path);

  parser.add_argument("-t", "--template-path")
      .help("Template directory path")
      .default_value("template")
      .store_into(template_path);

  // Positional: source file to parse
  parser.add_argument("source_file").help("Source file to parse").nargs(1).store_into(source_file);

  parser.add_argument("-i", "--include-path")
      .help("Include paths")
      .nargs(argparse::nargs_pattern::at_least_one)
      .store_into(include_paths);

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

  return true;
}