#include "CMakeAnalyzer/CompileCommands.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace fs = std::filesystem;

namespace Aternyx::CMake {

std::vector<std::string> SplitCommandLine(const std::string& command) {
  std::vector<std::string> args;
  std::string arg;
  bool inToken = false;  // an argument with no visible characters yet
  bool inQuotes = false;
  const size_t size = command.size();
  size_t i = 0;
  while (i < size) {
    const char c = command[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      if (inQuotes) {
        arg += c;
      } else if (inToken) {
        args.push_back(arg);
        arg.clear();
        inToken = false;
      }
      ++i;
      continue;
    }
    if (c == '"') {
      inQuotes = !inQuotes;
      inToken = true;
      ++i;
      continue;
    }
    if (c == '\\') {
      size_t slashes = 0;
      while (i < size && command[i] == '\\') {
        ++slashes;
        ++i;
      }
      if (i < size && command[i] == '"') {
        // 2n backslashes + quote: n backslashes, quote toggles quoting.
        // 2n+1 backslashes + quote: n backslashes + a literal quote.
        arg.append(slashes / 2, '\\');
        if (slashes % 2 == 0) {
          inQuotes = !inQuotes;
        } else {
          arg += '"';
        }
        ++i;
      } else {
        arg.append(slashes, '\\');
      }
      inToken = true;
      continue;
    }
    arg += c;
    inToken = true;
    ++i;
  }
  if (inToken || !arg.empty())
    args.push_back(arg);
  return args;
}

namespace {

// Response-file content: whitespace/newline separated, same quoting rules.
std::vector<std::string> ExpandArguments(const std::vector<std::string>& rawArguments) {
  std::vector<std::string> expanded;
  expanded.reserve(rawArguments.size());
  for (const auto& argument : rawArguments) {
    if (argument.size() > 1 && argument[0] == '@') {
      std::ifstream rsp{argument.substr(1), std::ios::binary};
      if (!rsp) {
        std::cerr << "[CMakeAnalyzer] warning: cannot open response file " << argument.substr(1)
                  << ", keeping the argument as-is" << std::endl;
        expanded.push_back(argument);
        continue;
      }
      std::stringstream buffer;
      buffer << rsp.rdbuf();
      for (auto& part : SplitCommandLine(buffer.str()))
        expanded.push_back(std::move(part));
    } else {
      expanded.push_back(argument);
    }
  }
  return expanded;
}

}  // namespace

std::string NormalizePath(const fs::path& baseDirectory, const std::string& path) {
  fs::path candidate{path};
  if (candidate.is_relative())
    candidate = baseDirectory / candidate;
  std::string normalized = candidate.lexically_normal().generic_string();
  // lexically_normal keeps a trailing separator when the last element is
  // ".." (e.g. "a/b/.." -> "a/"); drop it for stable comparisons.
  if (normalized.size() > 1 && normalized.back() == '/')
    normalized.pop_back();
  return normalized;
}

bool CompileCommandDb::Load(const std::string& path) {
  entries_.clear();
  lastError_.clear();

  fs::path dbPath{path};
  if (fs::is_directory(dbPath))
    dbPath /= "compile_commands.json";
  if (!fs::exists(dbPath)) {
    lastError_ = "compile_commands.json not found at: " + dbPath.string();
    return false;
  }

  std::ifstream ifs{dbPath, std::ios::binary};
  if (!ifs) {
    lastError_ = "cannot open file: " + dbPath.string();
    return false;
  }

  nlohmann::json root;
  try {
    ifs >> root;
  } catch (const std::exception& e) {
    lastError_ = std::string("failed to parse ") + dbPath.string() + ": " + e.what();
    return false;
  }

  if (!root.is_array()) {
    lastError_ = dbPath.string() + " does not contain a JSON array";
    return false;
  }

  entries_.reserve(root.size());
  for (const auto& item : root) {
    CompileEntry entry;
    try {
      if (auto it = item.find("directory"); it != item.end() && it->is_string())
        entry.directory = it->get<std::string>();
      if (auto it = item.find("file"); it != item.end() && it->is_string())
        entry.file = it->get<std::string>();
      if (auto it = item.find("output"); it != item.end() && it->is_string())
        entry.output = it->get<std::string>();
      if (auto it = item.find("arguments"); it != item.end() && it->is_array()) {
        for (const auto& argument : *it)
          if (argument.is_string())
            entry.arguments.push_back(argument.get<std::string>());
      } else if (auto it = item.find("command"); it != item.end() && it->is_string()) {
        entry.arguments = SplitCommandLine(it->get<std::string>());
      }
    } catch (const std::exception& e) {
      lastError_ = std::string("malformed compile db entry: ") + e.what();
      entries_.clear();
      return false;
    }

    if (entry.file.empty()) {
      std::cerr << "[CMakeAnalyzer] warning: entry without \"file\" skipped" << std::endl;
      continue;
    }
    entry.arguments = ExpandArguments(entry.arguments);
    entry.directory = entry.directory.empty() ? "." : entry.directory;
    entry.file = NormalizePath(entry.directory, entry.file);
    entry.output = NormalizePath(entry.directory, entry.output);
    entries_.push_back(std::move(entry));
  }

  if (entries_.empty()) {
    lastError_ = dbPath.string() + " contains no usable entries";
    return false;
  }
  return true;
}

}  // namespace Aternyx::CMake
