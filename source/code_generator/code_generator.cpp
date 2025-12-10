#include "code_generator/code_generator.h"

#include <algorithm>
#include <clang-c/Index.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "config/arg_config.h"
#include "utils/utils.h"

using Mustache = kainjow::mustache::mustache;
using MustacheData = kainjow::mustache::data;
namespace fs = std::filesystem;

namespace Aternyx {

enum class Priority {
  TYPE_A,  // only gen one file. no dependency.
  TYPE_B,  // gen file by
  TYPE_C,
  TYPE_D,
  TYPE_E,
};

struct TempInfo {
  std::string name;
  TempType type;
  Priority priority_type;
  std::string out_file_name;
};

static const std::vector<TempInfo> kTempConfigList = {
    {"EnumCast", TempType::SERIALIZATION, Priority::TYPE_A, "enum_cast.gen.h"},
    {"Serialization", TempType::SERIALIZATION, Priority::TYPE_B, ".gen.h"},
    {"ObjectHandleSerialization", TempType::SERIALIZATION, Priority::TYPE_B, "_object_handle.gen.h"},
    {"EditorUi", TempType::EDITOR_UI, Priority::TYPE_B, ".gen.h"},
    {"VisitEditorUi", TempType::EDITOR_UI, Priority::TYPE_C, "_visit_ui.gen.h"},
    {"Variant", TempType::REFLECTION, Priority::TYPE_B, "_variant.gen.h"},
    {"_AllInclude", TempType::NONE, Priority::TYPE_E, "all_include.gen.h"},
};

static const std::unordered_map<TempType, std::vector<std::string>> kPreIncludeFiles = {
    {
        TempType::SERIALIZATION,
        {"<yaml-cpp/yaml.h>", "\"precompile/core_serialization.h\""},
    },
    {
        TempType::EDITOR_UI,
        {"\"editor/editor_ui/utility/imgui_utility.h\"", "\"editor/editor_ui/utility/imgui_user_lib.h\""},
    },
    {
        TempType::REFLECTION,
        {},
    },
};

struct CodeGenerator::Impl {
  std::unordered_set<std::string> temp_type_a_list;
  std::unordered_map<TempType, std::vector<std::pair<Priority, std::string>>> generated_file_map;
};

CodeGenerator::CodeGenerator() {
  p_impl_ = new Impl();
}

CodeGenerator::~CodeGenerator() {
  delete p_impl_;
}

void CodeGenerator::Init() {
  std::filesystem::path k_meta_root_path{ArgConfig::Instance().template_path};
  uint32_t count = kTempConfigList.size();
  m_temp_list_.resize(count);
  for (int i = 0; i < count; i++) {
    auto& temp_name = kTempConfigList[i].name;
    m_temp_map_[temp_name] = -1;
    // load template file
    fs::path file_path{k_meta_root_path / (temp_name + ".mustache")};
    if (!std::filesystem::exists(file_path)) {
      continue;
    }
    std::ifstream ifs{file_path, std::ios::binary};
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    ifs.close();
    // create and validate template
    m_temp_list_[i] = kainjow::mustache::mustache{buffer.str()};
    if (!m_temp_list_[i].is_valid()) {
    } else {
      m_temp_list_[i].set_custom_escape(kainjow::mustache::trim<std::string>);
      m_temp_map_[temp_name] = i;
      if (kTempConfigList[i].priority_type == Priority::TYPE_A)
        p_impl_->temp_type_a_list.insert(temp_name);
    }
  }
}

void CodeGenerator::SetAstTree(AstTree* astTree) {
  mAstTree_ = astTree;
  InitMetaStructGroup();
}

std::vector<MetaStruct*>& CodeGenerator::TryGetMetaStructGroup(const std::string& key) {
  for (auto& group : metaStructGroups_) {
    if (group.first == key)
      return group.second;
  }
  metaStructGroups_.push_back({key, {}});
  return metaStructGroups_.back().second;
}

void CodeGenerator::InitMetaStructGroup() {
  for (auto& metaStruct : mAstTree_->metaStructList) {
    MetaStruct* pMetaStruct = &metaStruct;
    auto& group = TryGetMetaStructGroup(metaStruct.sourceFilePath);
    group.push_back(pMetaStruct);
    CreateMetaStructData(pMetaStruct);
  }
}

void CodeGenerator::CreateMetaStructData(Aternyx::MetaStruct* metaStruct) {
  MustacheData data;
  data.set("is_struct", metaStruct->kind == CXCursor_StructDecl);
  data.set("namespace", metaStruct->namespaceName);
  data.set("type_name", metaStruct->typeName);
  data.set("simple_type_name", metaStruct->simpleTypeName);
  if (metaStruct->kind == CXCursor_EnumDecl || !metaStruct->baseTypeName.empty())
    data.set("base_type_name", metaStruct->kind == CXCursor_EnumDecl ? "uint32_t" : metaStruct->baseTypeName);
  MustacheData childTypeList = MustacheData::type::list;
  size_t size = metaStruct->derivedTypeIndex.size();
  for (size_t i = 0; i < size; i++) {
    auto index = metaStruct->derivedTypeIndex.at(i);
    auto& derivedMeta = mAstTree_->GetMetaStruct(index);
    MustacheData itemData;
    itemData.set("child_is_struct", derivedMeta.kind == CXCursor_StructDecl);
    itemData.set("child_namespace", derivedMeta.namespaceName);
    itemData.set("child_simple_type_name", derivedMeta.simpleTypeName);
    itemData.set("child_type_name", derivedMeta.typeName);
    itemData.set("comma", i != (size - 1));
    itemData.set("custom_ui", GetAttribute(derivedMeta.attributes, "CustomUi"));
    itemData.set("editor_ui", GetAttribute(derivedMeta.attributes, "EditorUi"));
    childTypeList.push_back(itemData);
  }
  data.set("child_type_list", childTypeList);
  MustacheData propertyList = MustacheData::type::list;
  MustacheData functionList = MustacheData::type::list;
  for (auto& field : metaStruct->fields) {
    if (GetAttribute(field.attributes, "Runtime"))
      continue;
    MustacheData item;
    item.set("name", field.name);
    item.set("type_name", field.type);
    if (field.metaFieldType == MetaFieldTypeInfo::Property)
      propertyList.push_back(item);
    else {
      if (GetAttribute(field.attributes, "Serializable"))
        functionList.push_back(item);
    }
  }
  data.set("property_list", propertyList);
  data.set("function_list", functionList);
  metaStructDataMap_[metaStruct] = std::move(data);
}

void CodeGenerator::Run() {
  for (auto& [fileName, metaStructList] : metaStructGroups_) {
    if (metaStructList.empty())
      continue;
  }
  {
    for (const auto& tempName : p_impl_->temp_type_a_list) {
      if (m_temp_map_.at(tempName) == -1)
        continue;
      std::vector<MetaStruct*> matchedMetaStructList;
      for (auto& metaStruct : mAstTree_->metaStructList) {
        if (GetAttribute(metaStruct.attributes, tempName))
          matchedMetaStructList.push_back(&metaStruct);
      }
      if (!matchedMetaStructList.empty())
        GenFileByMetaStructList(tempName, "", matchedMetaStructList);
    }
  }
  {
    std::string filePath;
    std::string fileName;
    std::unordered_map<std::string, std::vector<MetaStruct*>> attributeMetaStructGroup;
    for (auto& metaStruct : mAstTree_->metaStructList) {
      if (filePath != metaStruct.sourceFilePath) {
        for (auto& [attribute, metaStructList] : attributeMetaStructGroup) {
          if (p_impl_->temp_type_a_list.contains(attribute) || !m_temp_map_.contains(attribute) ||
              m_temp_map_.at(attribute) == -1)
            continue;
          GenFileByMetaStructList(attribute, fileName, metaStructList);
        }
        attributeMetaStructGroup.clear();
        filePath = metaStruct.sourceFilePath;
        fileName = fs::path{filePath}.stem().string();
      }
      for (auto& attribute : metaStruct.attributes) {
        attributeMetaStructGroup[attribute].push_back(&metaStruct);
      }
    }
    for (auto& [attribute, metaStructList] : attributeMetaStructGroup) {
      if (p_impl_->temp_type_a_list.contains(attribute) || !m_temp_map_.contains(attribute) ||
          m_temp_map_.at(attribute) == -1)
        continue;
      GenFileByMetaStructList(attribute, fileName, metaStructList);
    }
    attributeMetaStructGroup.clear();
  }
  {
    for (auto& [tempType, generatedFiles] : p_impl_->generated_file_map) {
      if (generatedFiles.empty())
        continue;
      MustacheData data;
      MustacheData includeFileList = MustacheData::type::list;
      auto& preIncludedFiles = kPreIncludeFiles.at(tempType);
      for (auto& includedFile : preIncludedFiles) {
        includeFileList.push_back({"include_path", includedFile});
      }
      std::sort(generatedFiles.begin(), generatedFiles.end(), [](auto& a, auto& b) { return a.first < b.first; });
      for (auto& filePair : generatedFiles) {
        includeFileList.push_back({"include_path", "\"" + filePair.second + "\""});
      }
      data.set("include_file_list", includeFileList);
      GenFile("_AllInclude", "", data, tempType);
    }
  }
}

void CodeGenerator::GenFileByMetaStructList(const std::string& tempName,
                                            const std::string& fileName,
                                            const std::vector<MetaStruct*>& metaStructList) {
  MustacheData data;
  MustacheData metaTypeList = MustacheData::type::list;
  std::unordered_set<std::string> includeFiles;
  for (auto& pMetaStruct : metaStructList) {
    metaTypeList.push_back(metaStructDataMap_.at(pMetaStruct));
    includeFiles.insert(StringLib::GetRelativePath(pMetaStruct->sourceFilePath, ArgConfig::Instance().project_path));
  }
  data.set("meta_type_list", metaTypeList);
  MustacheData includeFileList = MustacheData::type::list;
  for (auto& filePath : includeFiles)
    includeFileList.push_back({"include_path", filePath});
  data.set("include_file_list", includeFileList);
  GenFile(tempName, fileName, data);
}

void CodeGenerator::GenFile(const std::string& temp_name,
                            const std::string& file_name,
                            const kainjow::mustache::data& data,
                            TempType override_type) {
  auto temp_index = m_temp_map_.at(temp_name);
  if (temp_index == -1) {
    return;
  }
  auto& temp_config = kTempConfigList[temp_index];
  auto result = m_temp_list_.at(temp_index).render(data);
  std::string out_file_name = file_name + temp_config.out_file_name;
  std::string file_path;

  std::string output_path = ArgConfig::Instance().output_path;

  TempType temp_type = override_type != TempType::NONE ? override_type : temp_config.type;
  switch (temp_type) {
    case TempType::SERIALIZATION:
      if (!fs::exists(output_path + "/serialization/"))
        fs::create_directories(output_path + "/serialization/");
      file_path = output_path + "/serialization/" + out_file_name;
      break;
    case TempType::EDITOR_UI:
      if (!fs::exists(output_path + "/editor_ui/"))
        fs::create_directories(output_path + "/editor_ui/");
      file_path = output_path + "/editor_ui/" + out_file_name;
      break;
    case TempType::REFLECTION:
      if (!fs::exists(output_path + "/reflection/"))
        fs::create_directories(output_path + "/reflection/");
      file_path = output_path + "/reflection/" + out_file_name;
      break;
    case TempType::NONE:
      break;
  }

  if (temp_config.type != TempType::NONE)
    p_impl_->generated_file_map[temp_type].push_back({temp_config.priority_type, out_file_name});

  std::ofstream ofs{file_path, std::ios::binary};
  ofs << result << std::endl;
  ofs.flush();
  ofs.close();
}

bool CodeGenerator::GetAttribute(const std::vector<std::string>& attributes, const std::string& attribute) {
  return std::find(attributes.begin(), attributes.end(), attribute) != attributes.end();
}
}  // namespace Aternyx