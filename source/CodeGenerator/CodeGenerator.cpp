#include "CodeGenerator/CodeGenerator.h"

#include <algorithm>
#include <clang-c/Index.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "Utils/Utils.h"

using Mustache = kainjow::mustache::mustache;
using MustacheData = kainjow::mustache::data;
namespace fs = std::filesystem;

namespace Aternyx {

bool ParseTempTypeCategory(const std::string& name, TempType& out) {
  if (StringLib::EqualsNoCase(name, "serialization")) {
    out = TempType::SERIALIZATION;
    return true;
  }
  if (StringLib::EqualsNoCase(name, "editor_ui")) {
    out = TempType::EDITOR_UI;
    return true;
  }
  if (StringLib::EqualsNoCase(name, "reflection")) {
    out = TempType::REFLECTION;
    return true;
  }
  return false;
}

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
  Priority priorityType;
  std::string outFileName;
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

// Built-in per-category prelude includes injected into each category's
// all_include.gen.h. Per-file generated headers assume the category
// aggregator (or an equivalent prelude) is included first. A
// CodegenConfig.preIncludes entry replaces these for that category, so a
// consumer project can declare its own runtime dependencies.
static const std::unordered_map<TempType, std::vector<std::string>> kDefaultPreIncludeFiles = {
    {
        TempType::SERIALIZATION,
        {"<yaml-cpp/yaml.h>", "\"precompile/core_serialization.h\"", "<ucore/struct/object_pool.h>",
         "\"precompile/component_serialization.h\""},
    },
    {
        TempType::EDITOR_UI,
        {"\"editor/editor_ui/utility/imgui_utility.h\"", "\"editor/editor_ui/utility/imgui_user_lib.h\""},
    },
    {
        TempType::REFLECTION,
        {"<ucore/struct/object_pool.h>"},
    },
};

// Sub-directory names per generated category, for each supported path style.
struct SubDirNames {
  const char* snake;
  const char* camel;
};
static const std::unordered_map<TempType, SubDirNames> kSubDirNames = {
    {TempType::SERIALIZATION, {"serialization", "Serialization"}},
    {TempType::EDITOR_UI, {"editor_ui", "EditorUi"}},
    {TempType::REFLECTION, {"reflection", "Reflection"}},
};

struct CodeGenerator::Impl {
  std::unordered_set<std::string> tempTypeAList;
  std::unordered_map<TempType, std::vector<std::pair<Priority, std::string>>> generatedFileMap;
};

CodeGenerator::CodeGenerator() : impl_(std::make_unique<Impl>()) {}

CodeGenerator::~CodeGenerator() = default;

void CodeGenerator::Init(const CodegenConfig& config) {
  config_ = config;
  std::filesystem::path metaRootPath{config_.templatePath};
  uint32_t count = kTempConfigList.size();
  tempList_.resize(count);
  for (int i = 0; i < count; i++) {
    auto& tempName = kTempConfigList[i].name;
    tempMap_[StringLib::ToLower(tempName)] = -1;
    fs::path filePath{metaRootPath / (tempName + ".mustache")};
    if (!std::filesystem::exists(filePath)) {
      std::cerr << "[CodeGenerator] warning: template not found, skipped: " << filePath.string() << std::endl;
      continue;
    }
    std::ifstream ifs{filePath, std::ios::binary};
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    ifs.close();
    tempList_[i] = kainjow::mustache::mustache{buffer.str()};
    if (!tempList_[i].is_valid()) {
      throw std::runtime_error("Invalid mustache template: " + filePath.string() + " (" +
                               tempList_[i].error_message() + ")");
    } else {
      tempList_[i].set_custom_escape(kainjow::mustache::trim<std::string>);
      tempMap_[StringLib::ToLower(tempName)] = i;
      if (kTempConfigList[i].priorityType == Priority::TYPE_A)
        impl_->tempTypeAList.insert(StringLib::ToLower(tempName));
    }
  }
}

void CodeGenerator::SetAstTree(AstTree* astTree) {
  astTree_ = astTree;
  InitMetaStructGroup();
}

void CodeGenerator::InitMetaStructGroup() {
  for (auto& metaStruct : astTree_->metaStructList) {
    CreateMetaStructData(&metaStruct);
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
    auto& derivedMeta = astTree_->GetMetaStruct(index);
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
  PlanJobs();
  for (const auto& job : jobs_)
    RenderJob(job);
  GenAllIncludes();
}

void CodeGenerator::PlanJobs() {
  jobs_.clear();
  impl_->generatedFileMap.clear();
  genFilesBySource_.clear();

  // TYPE_A templates aggregate every annotated type of the whole run into a
  // single output file (e.g. enum_cast.gen.h).
  for (const auto& tempName : impl_->tempTypeAList) {
    if (tempMap_.at(tempName) == -1)
      continue;
    std::vector<MetaStruct*> matchedMetaStructList;
    for (auto& metaStruct : astTree_->metaStructList) {
      if (GetAttribute(metaStruct.attributes, tempName))
        matchedMetaStructList.push_back(&metaStruct);
    }
    if (!matchedMetaStructList.empty())
      jobs_.push_back({tempName, "", "", std::move(matchedMetaStructList)});
  }

  // Remaining templates emit one file per source file, aggregating the types
  // annotated with the template's name. metaStructList is ordered by source
  // file, so a group ends whenever the file changes.
  std::string filePath;
  std::string fileName;
  std::unordered_map<std::string, std::vector<MetaStruct*>> attributeMetaStructGroup;
  auto flushGroup = [&]() {
    for (auto& [attribute, metaStructList] : attributeMetaStructGroup) {
      if (!tempMap_.contains(attribute) || tempMap_.at(attribute) == -1 ||
          impl_->tempTypeAList.contains(attribute))
        continue;
      jobs_.push_back({attribute, fileName, metaStructList.front()->sourceFilePath, metaStructList});
    }
    attributeMetaStructGroup.clear();
  };
  for (auto& metaStruct : astTree_->metaStructList) {
    if (filePath != metaStruct.sourceFilePath) {
      flushGroup();
      filePath = metaStruct.sourceFilePath;
      fileName = fs::path{filePath}.stem().string();
    }
    for (auto& attribute : metaStruct.attributes) {
      attributeMetaStructGroup[StringLib::ToLower(attribute)].push_back(&metaStruct);
    }
  }
  flushGroup();

  // Pre-register every planned output before rendering anything: the
  // gen_include_list of one file may reference outputs that are only
  // rendered later (e.g. a visit-ui file referencing the variant file).
  for (auto& job : jobs_) {
    const auto& tempConfig = kTempConfigList[tempMap_.at(StringLib::ToLower(job.tempName))];
    if (tempConfig.type == TempType::NONE)
      continue;
    std::string outFileName = job.fileName + tempConfig.outFileName;
    impl_->generatedFileMap[tempConfig.type].push_back({tempConfig.priorityType, outFileName});
    if (!job.sourceFile.empty() && tempConfig.priorityType != Priority::TYPE_A) {
      genFilesBySource_[StringLib::NormalizePath(job.sourceFile)].push_back(
          {std::move(outFileName), ResolveOutputDir(tempConfig.type).generic_string()});
    }
  }
}

void CodeGenerator::RenderJob(const GenJob& job) {
  auto tempIndex = tempMap_.at(StringLib::ToLower(job.tempName));
  const auto& tempConfig = kTempConfigList[tempIndex];
  const fs::path outDir = ResolveOutputDir(tempConfig.type);
  const std::string outDirString = outDir.generic_string();

  MustacheData data;
  MustacheData metaTypeList = MustacheData::type::list;
  // std::set keeps the include list sorted and free of duplicates, so the
  // output is deterministic regardless of AST ordering.
  std::set<std::string> includeFiles;
  for (auto& pMetaStruct : job.metaStructList) {
    metaTypeList.push_back(metaStructDataMap_.at(pMetaStruct));
    includeFiles.insert(
        StringLib::MakeIncludeSpelling(pMetaStruct->sourceFilePath, outDirString, config_.includeRoots));
  }
  data.set("meta_type_list", metaTypeList);
  MustacheData includeFileList = MustacheData::type::list;
  for (auto& filePath : includeFiles)
    includeFileList.push_back({"include_path", filePath});
  data.set("include_file_list", includeFileList);

  // Generated outputs that must be visible when compiling this output:
  //   - sibling outputs derived from the same source file (e.g. the variant
  //     file a visit-ui file operates on),
  //   - outputs of annotated headers the source file (transitively) includes,
  //     because generated code references their specializations — the
  //     `convert<Guid>` used by an emitted `as<Guid>()` lives in the Guid
  //     output, not in the (already included) Guid source header.
  MustacheData genIncludeList = MustacheData::type::list;
  if (!job.sourceFile.empty()) {
    const std::string normalizedSource = StringLib::NormalizePath(job.sourceFile);
    const std::string self = job.fileName + tempConfig.outFileName;
    std::set<std::string> genIncludes;
    auto collect = [&](const std::string& sourceKey) {
      if (auto it = genFilesBySource_.find(sourceKey); it != genFilesBySource_.end()) {
        for (const auto& ref : it->second) {
          // Skip this output itself (same name and same output directory —
          // equal names can occur across category sub-directories).
          if (ref.fileName == self && ref.dir == outDirString)
            continue;
          fs::path rel = (fs::path{ref.dir} / ref.fileName).lexically_relative(outDir);
          genIncludes.insert(rel.empty() ? ref.fileName : StringLib::GetUnixPath(rel.generic_string()));
        }
      }
    };
    collect(normalizedSource);
    if (auto it = config_.sourceIncludes.find(normalizedSource); it != config_.sourceIncludes.end()) {
      for (const auto& includedFile : it->second)
        collect(includedFile);
    }
    for (auto& spelling : genIncludes)
      genIncludeList.push_back({"gen_include_path", spelling});
  }
  data.set("gen_include_list", genIncludeList);

  fs::create_directories(outDir);
  std::string result = tempList_.at(tempIndex).render(data);
  std::ofstream ofs{outDir / (job.fileName + tempConfig.outFileName), std::ios::binary};
  ofs << result << std::endl;
}

void CodeGenerator::GenAllIncludes() {
  for (auto& [tempType, generatedFiles] : impl_->generatedFileMap) {
    if (generatedFiles.empty())
      continue;
    auto tempIndex = tempMap_.at(StringLib::ToLower("_AllInclude"));
    const auto& tempConfig = kTempConfigList[tempIndex];

    MustacheData data;
    MustacheData includeFileList = MustacheData::type::list;
    for (auto& includedFile : PreludeFor(tempType)) {
      includeFileList.push_back({"include_path", includedFile});
    }
    std::sort(generatedFiles.begin(), generatedFiles.end(), [](auto& a, auto& b) {
      if (a.first != b.first)
        return a.first < b.first;
      return a.second < b.second;
    });
    for (auto& filePair : generatedFiles) {
      // The aggregator lives in the same directory as the files it lists, so
      // the bare file name is the correct relative spelling.
      includeFileList.push_back({"include_path", "\"" + filePair.second + "\""});
    }
    data.set("include_file_list", includeFileList);

    const fs::path outDir = ResolveOutputDir(tempType);
    fs::create_directories(outDir);
    std::string result = tempList_.at(tempIndex).render(data);
    std::ofstream ofs{outDir / tempConfig.outFileName, std::ios::binary};
    ofs << result << std::endl;
  }
}

fs::path CodeGenerator::ResolveOutputDir(TempType tempType) const {
  const SubDirNames& subDirNames = kSubDirNames.at(tempType);
  const char* subDir = config_.pathStyle == GenPathStyle::CamelCase ? subDirNames.camel : subDirNames.snake;
  return fs::path{config_.outputPath} / subDir;
}

const std::vector<std::string>& CodeGenerator::PreludeFor(TempType tempType) const {
  if (auto it = config_.preIncludes.find(tempType); it != config_.preIncludes.end())
    return it->second;
  return kDefaultPreIncludeFiles.at(tempType);
}

bool CodeGenerator::GetAttribute(const std::vector<std::string>& attributes, const std::string& attribute) {
  return std::find_if(attributes.begin(), attributes.end(),
                      [&attribute](const std::string& item) { return StringLib::EqualsNoCase(item, attribute); }) !=
         attributes.end();
}
}  // namespace Aternyx
