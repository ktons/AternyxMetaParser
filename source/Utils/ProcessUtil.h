#pragma once

#include <string>

namespace Aternyx::ProcessUtil {

// Runs `command` through the system shell with `input` on its stdin and
// returns the command's stdout. Before execution, every "%F" in the command
// is replaced with the shell-quoted `fileName` (e.g. the generated file's
// name, for tools that inspect it). This is the escape hatch that lets a
// configuration file carry behavior — e.g. formatting generated code with
// `post_process = "clang-format --style=file"` — without building a custom
// parser tool.
//
// Throws std::runtime_error when the command cannot be spawned or exits
// non-zero (fail-fast, matching the rest of the codegen pipeline). The
// command text comes from the project's own configuration and runs with the
// parser's privileges.
std::string RunFilter(const std::string& command, const std::string& fileName, const std::string& input);

}  // namespace Aternyx::ProcessUtil
