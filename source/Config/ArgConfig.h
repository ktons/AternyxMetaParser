#pragma once

#include <string>
#include <vector>

#include "Config/GenPathStyle.h"

namespace Aternyx {

// Which top-level operation the tool should run.
enum class CliMode {
  // Parse one source file and generate code into the output directory.
  // This is the classic behavior and the default.
  Codegen,
  // Analyze a CMake compile database (compile_commands.json): report the
  // include paths of every target, or generate code for one target with
  // --target.
  CMake,
};

}  // namespace Aternyx

// ArgConfig holds command-line options for the parser tool.
// Fields are populated by `ParseArgs(...)` using `argparse`.
struct ArgConfig {
  // Operation mode selected via --codegen / --cmake. Defaults to Codegen.
  Aternyx::CliMode mode{Aternyx::CliMode::Codegen};

  // Path to the root of the project being parsed. Optional.
  std::string projectPath_;

  // Path to the template directory used for code generation. Optional.
  std::string templatePath_;

  std::string outputPath_;

  // codegen mode: the source file to parse.
  // cmake mode: path to compile_commands.json (or a directory holding it).
  // This is expected to be a positional argument.
  std::string sourceFile_;

  std::vector<std::string> includePaths_;

  // Optional path to a TOML config file. If provided, the TOML file
  // values will be used to override fields in this struct.
  std::string tomlPath_;

  // cmake mode only: name of the target to run code generation for.
  // When empty, cmake mode only reports the analyzed targets.
  std::string target_;

  // Naming style of the generated sub-directories.
  Aternyx::GenPathStyle genPathStyle_{Aternyx::GenPathStyle::SnakeCase};

  // Returns the singleton instance of ArgConfig. This allows convenient
  // global access to parsed options. The instance is lazily constructed.
  static ArgConfig& Instance() {
    static ArgConfig s_config;
    return s_config;
  }

  // Parse command-line arguments. Fills the struct fields on success and
  // returns true. On parse error returns false (and writes a message to
  // `std::cerr`). If a TOML config file is supplied via `--toml`, its values
  // are applied first; explicitly provided command-line options win over the
  // TOML file, and TOML values win over built-in defaults.
  bool ParseArgs(int argc, char* argv[]);

  // Validate currently-stored configuration. Returns true when the fields
  // required by the selected mode are present.
  bool Validate();

  // Print debug information to stdout.
  void DebugInfo();

  // Parse and apply configuration from a TOML file. Returns true on
  // success and false on error. Keys are read from the file root and from an
  // optional [parserParams] table. Supported keys: output_path, project_path,
  // template_path, include_paths, target, gen_path_style.
  bool ParseTomlConfig(const char* tomlConfigPath);
};
