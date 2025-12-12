#pragma once

#include <string>
#include <vector>

// ArgConfig holds command-line options for the parser tool.
// Fields are populated by `Parse(...)` using `argparse`.
struct ArgConfig {
  // Path to the root of the project being parsed. Optional.
  std::string project_path;

  // Path to the template directory used for code generation. Optional.
  std::string template_path;

  std::string output_path;

  // Input source file to parse. This is expected to be a positional
  // argument and typically required.
  std::string source_file;

  std::vector<std::string> include_paths;
  // Optional path to a TOML config file. If provided, the TOML file
  // values will be used to override fields in this struct.
  std::string toml_path;

  // Returns the singleton instance of ArgConfig. This allows convenient
  // global access to parsed options. The instance is lazily constructed.
  static ArgConfig& Instance() {
    static ArgConfig s_config;
    return s_config;
  }

  // Parse command-line arguments. Fills the struct fields on success and
  // returns true. On parse error returns false (and writes a message to
  // `std::cerr`). If a TOML config file is supplied via `--toml`, the
  // contents will be parsed and used to override the fields in this
  // struct (see `ParseTomlConfig`).
  bool ParseArgs(int argc, char* argv[]);

  // Validate currently-stored configuration. Returns true when minimal
  // required fields are present.
  bool Validate();

  // Print debug information to stdout.
  void DebugInfo();

  // Parse and apply configuration from a TOML file. Returns true on
  // success and false on error. Values present in the TOML file will
  // overwrite the corresponding members of this object.
  bool ParseTomlConfig(const char* toml_config_path);
};
