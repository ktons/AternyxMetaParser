#pragma once

#include <filesystem>
#include <mustache.hpp>
#include <unordered_map>

#include "Parser/MetaInfo.h"

namespace Aternyx {

enum class TempType {
  REFLECTION,
  SERIALIZATION,
  EDITOR_UI,
  NONE,
};

class CodeGenerator {
 public:
  CodeGenerator();
  ~CodeGenerator();
  void Init();
  void SetAstTree(AstTree* astTree);
  void Run();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  std::unordered_map<std::string, int> tempMap_;
  std::vector<kainjow::mustache::mustache> tempList_;
  std::vector<std::string> serializationHeads_;
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

  // serialization
  void GenEnumMetaFile();
  void GenTypeSerializationFile(const std::string& sourcePath, const std::vector<MetaStruct*>& metaStructGroup);
  void GenObjectHandleSerialization(const MetaStruct& baseMeta);
  void GenIncludedHeadFile();
};
}  // namespace Aternyx
