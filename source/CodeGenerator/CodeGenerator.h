#pragma once

#include <filesystem>
#include <mustache.hpp>
#include <unordered_map>

#include "Config/GenPathStyle.h"
#include "Parser/MetaInfo.h"

namespace Aternyx {

enum class TempType {
  REFLECTION,
  SERIALIZATION,
  EDITOR_UI,
  NONE,
};

// Explicit configuration for code generation. Keeps CodeGenerator free of
// global singletons so it can be driven programmatically (e.g. one run per
// CMake target with its own output directory).
struct CodegenConfig {
  // Root directory all generated files are written into.
  std::string outputPath;
  // Directory holding the mustache templates.
  std::string templatePath;
  // Project root used to make generated `#include` lines relative.
  std::string projectPath;
  // Naming style of the generated sub-directories.
  GenPathStyle pathStyle{GenPathStyle::SnakeCase};
};

class CodeGenerator {
 public:
  CodeGenerator();
  ~CodeGenerator();
  // Loads the templates listed in `kTempConfigList` from `config.templatePath`.
  // Throws std::runtime_error when a template file exists but is not valid
  // mustache. Missing template files are optional and only warn on stderr.
  void Init(const CodegenConfig& config);
  void SetAstTree(AstTree* astTree);
  void Run();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  CodegenConfig config_;
  std::unordered_map<std::string, int> tempMap_;
  std::vector<kainjow::mustache::mustache> tempList_;
  AstTree* astTree_{nullptr};
  std::vector<std::pair<std::string, std::vector<MetaStruct*>>> metaStructGroups_;
  std::unordered_map<MetaStruct*, kainjow::mustache::data> metaStructDataMap_;

  std::vector<MetaStruct*>& TryGetMetaStructGroup(const std::string& key);
  void InitMetaStructGroup();

  void CreateMetaStructData(MetaStruct* metaStruct);

  bool GetAttribute(const std::vector<std::string>& attributes, const std::string& attribute);

  void GenFileByMetaStructList(const std::string& tempName,
                               const std::string& fileName,
                               const std::vector<MetaStruct*>& metaStructList);
  void GenFile(const std::string& tempName,
               const std::string& fileName,
               const kainjow::mustache::data& data,
               TempType overrideType = TempType::NONE);
};
}  // namespace Aternyx
