#pragma once

#include <string>
#include <vector>

#include "Config/GenPathStyle.h"

// ArgConfig holds command-line options for the parser tool. The tool has a
// single mode: analyze a CMake compile database (compile_commands.json) and
// either report the analyzed targets or run code generation for one target
// (--target). Fields are populated by `ParseArgs(...)` using `argparse`.
struct ArgConfig {
  // Path to the compile database (compile_commands.json, or a directory
  // holding it). Required positional argument.
  std::string compileDbPath_;

  std::string outputPath_;

  // Path to the template directory used for code generation.
  std::string templatePath_;

  // Extra header scan root for parse_headers mode (the CMake PROJECT_ROOT
  // option). Optional; the target's own source directories are always
  // scanned.
  std::string projectPath_;

  std::vector<std::string> includePaths_;

  // cmake mode: name of the target to run code generation for.
  // When empty, only reports the analyzed targets.
  std::string target_;

  // parse_headers mode: parse annotated headers instead of the target's
  // .cpp files (.h-as-source). Headers must be self-contained and must not
  // include generated files, so the first run never fails on missing
  // outputs. Settable via TOML `parse_headers`.
  bool parseHeaders_{false};

  // Text markers a header must contain to be parsed in parse_headers mode
  // (e.g. "CLASS("). Empty means the built-in defaults. Settable via TOML
  // `header_markers`.
  std::vector<std::string> headerMarkers_;

  // Naming style of the generated sub-directories. Settable via TOML
  // `gen_path_style`.
  Aternyx::GenPathStyle genPathStyle_{Aternyx::GenPathStyle::SnakeCase};

  // Returns the singleton instance of ArgConfig. This allows convenient
  // global access to parsed options. The instance is lazily constructed.
  static ArgConfig& Instance() {
    static ArgConfig s_config;
    return s_config;
  }

  // Parse command-line arguments. Fills the struct fields on success and
  // returns true. On parse error returns false (and writes a message to
  // `std::cerr`). If a TOML config file is supplied via `--toml`, its
  // project-independent settings are applied on top of the built-in
  // defaults; command-line options always win.
  bool ParseArgs(int argc, char* argv[]);

  // Validate currently-stored configuration. Returns true when the required
  // fields are present.
  bool Validate();

  // Print debug information to stdout.
  void DebugInfo();

  // Parse the project-independent settings from a TOML file. Root-level
  // keys only: gen_path_style, parse_headers, header_markers. Returns true
  // on success and false on error.
  bool ParseTomlConfig(const char* tomlConfigPath);
};
