#include "CMakeAnalyzer/CMakeTargetAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace Aternyx::CMake {

namespace {

constexpr const char* kCppSourceExtensions[] = {".cpp", ".cc", ".cxx", ".c++"};

bool IsCppSource(const std::string& path) {
  std::string extension = fs::path{path}.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (const char* known : kCppSourceExtensions) {
    if (extension == known)
      return true;
  }
  return false;
}

// Extracts the target name from an object file path such as
// "build/Source/Runtime/CMakeFiles/NyxCoreUtils.dir/Asset.cpp.obj".
// Returns false when the path does not reference a CMake target.
bool TargetNameFromObjectPath(const std::string& objectPath, std::string& targetName) {
  const std::string marker = "CMakeFiles";
  size_t position = objectPath.find(marker);
  if (position == std::string::npos)
    return false;
  position += marker.size();
  if (position < objectPath.size() && (objectPath[position] == '/' || objectPath[position] == '\\'))
    ++position;
  const size_t end = objectPath.find(".dir", position);
  if (end == std::string::npos || end == position)
    return false;
  targetName = objectPath.substr(position, end - position);
  return true;
}

// Adds a value to `values` unless already present (first-seen order kept).
void AddUnique(std::vector<std::string>& values, std::string value) {
  if (std::find(values.begin(), values.end(), value) == values.end())
    values.push_back(std::move(value));
}

struct ExtractedFlags {
  std::vector<std::string> includePaths;
  std::vector<std::string> defines;
  std::string cxxStandard;
};

// Collects include paths, defines and the language standard from a compiler
// argument list. Both GNU ("-I", "-D", "-std=") and MSVC ("/I", "/D",
// "/std:") spellings are accepted in attached and separate form, including
// the dash-colon "-std:c++17" spelling CMake emits for MSVC.
ExtractedFlags ExtractFlags(const std::vector<std::string>& arguments, const fs::path& directory) {
  ExtractedFlags flags;
  auto addPath = [&](std::string path) {
    if (path.empty())
      return;
    AddUnique(flags.includePaths, NormalizePath(directory, path));
  };

  for (size_t i = 0; i < arguments.size(); ++i) {
    const std::string& argument = arguments[i];
    if (argument.size() < 2)
      continue;

    bool isInclude = false;
    bool isDefine = false;
    std::string attached;
    if (argument.rfind("-I", 0) == 0 || argument.rfind("/I", 0) == 0) {
      isInclude = true;
      attached = argument.substr(2);
    } else if (argument.rfind("-isystem", 0) == 0) {
      isInclude = true;
      attached = argument.substr(8);
    } else if (argument.rfind("-D", 0) == 0 || argument.rfind("/D", 0) == 0) {
      isDefine = true;
      attached = argument.substr(2);
    }

    if (isInclude) {
      if (!attached.empty()) {
        addPath(attached);
      } else if (i + 1 < arguments.size()) {
        addPath(arguments[++i]);
      }
      continue;
    }
    if (isDefine) {
      if (attached.empty() && i + 1 < arguments.size())
        attached = arguments[++i];
      if (!attached.empty())
        AddUnique(flags.defines, "-D" + attached);
      continue;
    }

    // Language standard: -std=c++17, -std:c++17 or /std:c++17.
    std::string value;
    if (argument.rfind("-std=", 0) == 0)
      value = argument.substr(5);
    else if (argument.rfind("-std:", 0) == 0 || argument.rfind("/std:", 0) == 0)
      value = argument.substr(5);
    if (!value.empty() && flags.cxxStandard.empty()) {
      // "c++latest" is an MSVC/clang-cl spelling; the GNU driver used by
      // libclang does not accept it. Map it to the equivalent standard that
      // every supported toolchain understands.
      if (value == "c++latest")
        value = "c++26";
      flags.cxxStandard = "-std=" + value;
    }
  }
  return flags;
}

}  // namespace

bool CMakeTargetAnalyzer::Analyze(const std::string& dbPath) {
  targets_.clear();
  lastError_.clear();

  CompileCommandDb db;
  if (!db.Load(dbPath)) {
    lastError_ = db.LastError();
    return false;
  }

  std::unordered_map<std::string, size_t> targetIndex;
  auto findOrInsertTarget = [&](const std::string& name) -> CMakeTarget& {
    auto it = targetIndex.find(name);
    if (it == targetIndex.end()) {
      targetIndex.emplace(name, targets_.size());
      targets_.push_back(CMakeTarget{});
      targets_.back().name = name;
      return targets_.back();
    }
    return targets_[it->second];
  };

  for (const CompileEntry& entry : db.Entries()) {
    std::string targetName;
    if (!TargetNameFromObjectPath(entry.output, targetName)) {
      // Fall back to the -o / /Fo argument of the command line.
      for (size_t i = 0; i < entry.arguments.size() && targetName.empty(); ++i) {
        const std::string& argument = entry.arguments[i];
        std::string objectPath;
        if (argument.rfind("-o", 0) == 0 && argument.size() > 2) {
          objectPath = argument.substr(2);
        } else if (argument == "-o") {
          if (i + 1 < entry.arguments.size())
            objectPath = entry.arguments[i + 1];
        } else if (argument.rfind("/Fo", 0) == 0) {
          if (argument.size() > 3)
            objectPath = argument.substr(3);
          else if (i + 1 < entry.arguments.size())
            objectPath = entry.arguments[i + 1];
        }
        TargetNameFromObjectPath(objectPath, targetName);
      }
    }
    if (targetName.empty()) {
      std::cerr << "[CMakeAnalyzer] warning: cannot determine target for " << entry.file
                << ", entry skipped" << std::endl;
      continue;
    }
    if (!IsCppSource(entry.file))
      continue;

    CMakeTarget& target = findOrInsertTarget(targetName);
    AddUnique(target.cppFiles, entry.file);

    const ExtractedFlags flags = ExtractFlags(entry.arguments, fs::path{entry.directory});
    for (const std::string& includePath : flags.includePaths)
      AddUnique(target.includePaths, includePath);
    for (const std::string& define : flags.defines)
      AddUnique(target.defines, define);
    if (target.cxxStandard.empty())
      target.cxxStandard = flags.cxxStandard;
  }

  // A database without any C++ target is not an error: callers decide how to
  // react (the report shows nothing, --target lookup fails with a message).
  return true;
}

const CMakeTarget* CMakeTargetAnalyzer::FindTarget(const std::string& name) const {
  for (const CMakeTarget& target : targets_) {
    if (target.name == name)
      return &target;
  }
  return nullptr;
}

}  // namespace Aternyx::CMake
