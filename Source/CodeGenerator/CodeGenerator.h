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
  Impl* p_impl_;

  std::unordered_map<std::string, int> m_temp_map_;
  std::vector<kainjow::mustache::mustache> m_temp_list_;
  std::vector<std::string> m_serialization_heads_;
  AstTree* mAstTree_;
  std::vector<std::pair<std::string, std::vector<MetaStruct*>>> metaStructGroups_;
  std::unordered_map<MetaStruct*, kainjow::mustache::data> metaStructDataMap_;

  const std::filesystem::path k_source_path_ = {__SOURCE_PATH__};
  std::vector<MetaStruct*>& TryGetMetaStructGroup(const std::string& key);
  void InitMetaStructGroup();

  void CreateMetaStructData(MetaStruct* metaStruct);

  bool GetAttribute(const std::vector<std::string>& attributes, const std::string& attribute);

  void GenFileByMetaStructList(const std::string& tempName,
                               const std::string& fileName,
                               const std::vector<MetaStruct*>& metaStructList);
  void GenFile(const std::string& temp_name,
               const std::string& file_name,
               const kainjow::mustache::data& data,
               TempType override_type = TempType::NONE);

  // serialization
  void GenEnumMetaFile();
  void GenTypeSerializationFile(const std::string& sourcePath, const std::vector<MetaStruct*>& metaStructGroup);
  void GenObjectHandleSerialization(const MetaStruct& baseMeta);
  void GenIncludedHeadFile();
};
}  // namespace Aternyx