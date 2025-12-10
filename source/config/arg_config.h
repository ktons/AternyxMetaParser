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

  // Returns the singleton instance of ArgConfig. This allows convenient
  // global access to parsed options. The instance is lazily constructed.
  static ArgConfig& Instance() {
    static ArgConfig s_config;
    return s_config;
  }

  // Parse command-line arguments. Fills the struct fields on success and
  // returns true. On parse error returns false (and writes a message to
  // `std::cerr`).
  bool ParseArgs(int argc, char* argv[]);

  bool Validate();

  void DebugInfo();
};
