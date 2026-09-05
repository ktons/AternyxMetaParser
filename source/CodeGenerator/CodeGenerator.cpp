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

std::vector<CodegenCategory> DefaultCodegenCategories() {
  return {
      {std::string(kCategorySerialization), "serialization", "Serialization"},
      {std::string(kCategoryEditorUi), "editor_ui", "EditorUi"},
      {std::string(kCategoryReflection), "reflection", "Reflection"},
  };
}

std::vector<TemplateDesc> DefaultTemplates() {
  return {
      {"EnumCast", std::string(kCategorySerialization), OutputKind::GlobalAggregate, 0, "enum_cast.gen.h"},
      {"Serialization", std::string(kCategorySerialization), OutputKind::PerSourceFile, 100, ".gen.h"},
      {"ObjectHandleSerialization", std::string(kCategorySerialization), OutputKind::PerSourceFile, 100,
       "_object_handle.gen.h"},
      {"EditorUi", std::string(kCategoryEditorUi), OutputKind::PerSourceFile, 100, ".gen.h"},
      {"VisitEditorUi", std::string(kCategoryEditorUi), OutputKind::PerSourceFile, 200, "_visit_ui.gen.h", "",
       {"EditorUi", "Variant"}},
      {"Variant", std::string(kCategoryReflection), OutputKind::PerSourceFile, 100, "_variant.gen.h"},
  };
}

std::vector<CodegenAggregator> DefaultAggregators() {
  return {
      {std::string(kCategorySerialization), "_AllInclude", "all_include.gen.h"},
      {std::string(kCategoryEditorUi), "_AllInclude", "all_include.gen.h"},
      {std::string(kCategoryReflection), "_AllInclude", "all_include.gen.h"},
  };
}

// Per-category output directories: category name -> (snakeCaseDir, camelCaseDir).
struct CategoryDirs {
  std::string snakeDir;
  std::string camelDir;
};

struct CodeGenerator::Impl {
  std::unordered_map<std::string, CategoryDirs> categoryDirs;
  // Planned outputs per category: (producer order, output name). Filled in
  // RegisterOutputs, sorted there and consumed by the aggregator jobs.
  std::unordered_map<std::string, std::vector<std::pair<int, std::string>>> outputsByCategory;
};

CodeGenerator::CodeGenerator() : impl_(std::make_unique<Impl>()) {}

CodeGenerator::~CodeGenerator() = default;

void CodeGenerator::Init(const CodegenConfig& config) {
  Init(config, CodegenHooks{});
}

void CodeGenerator::Init(const CodegenConfig& config, CodegenHooks hooks) {
  config_ = config;
  hooks_ = std::move(hooks);

  // Empty lists keep the built-in registry; non-empty lists replace it.
  categories_ = config_.categories.empty() ? DefaultCodegenCategories() : config_.categories;
  templates_ = config_.templates.empty() ? DefaultTemplates() : config_.templates;
  aggregators_ = config_.aggregators.empty() ? DefaultAggregators() : config_.aggregators;

  impl_->categoryDirs.clear();
  for (const CodegenCategory& category : categories_) {
    if (category.name.empty())
      throw std::runtime_error("codegen registry: category with empty name");
    impl_->categoryDirs[category.name] =
        CategoryDirs{category.snakeDir, category.camelDir.empty() ? category.snakeDir : category.camelDir};
  }

  templateIndex_.clear();
  loadedTemplates_.clear();
  const fs::path metaRootPath{config_.templatePath};

  // Loads "<name>.mustache" once per lowercased name; returns false when the
  // file is missing (warned — an opt-out switch), throws when it is invalid.
  auto loadTemplate = [&](const std::string& name) -> bool {
    const std::string key = StringLib::ToLower(name);
    if (loadedTemplates_.contains(key))
      return true;
    fs::path filePath{metaRootPath / (name + ".mustache")};
    if (!std::filesystem::exists(filePath)) {
      std::cerr << "[CodeGenerator] warning: template not found, skipped: " << filePath.string() << std::endl;
      return false;
    }
    std::ifstream ifs{filePath, std::ios::binary};
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    ifs.close();
    kainjow::mustache::mustache loaded{buffer.str()};
    if (!loaded.is_valid()) {
      throw std::runtime_error("Invalid mustache template: " + filePath.string() + " (" + loaded.error_message() +
                               ")");
    }
    loaded.set_custom_escape(kainjow::mustache::trim<std::string>);
    loadedTemplates_[key] = std::move(loaded);
    return true;
  };

  for (size_t i = 0; i < templates_.size(); i++) {
    const TemplateDesc& desc = templates_[i];
    if (!impl_->categoryDirs.contains(desc.category))
      throw std::runtime_error("codegen registry: template '" + desc.name +
                               "' references unregistered category '" + desc.category + "'");
    if (desc.outputKind == OutputKind::CategoryAggregator)
      throw std::runtime_error("codegen registry: template '" + desc.name +
                               "' uses OutputKind::CategoryAggregator; register a CodegenAggregator instead");
    if (!templateIndex_.try_emplace(StringLib::ToLower(desc.name), i).second)
      throw std::runtime_error("codegen registry: duplicate template name '" + desc.name + "'");
    loadTemplate(desc.name);
  }

  std::unordered_set<std::string> aggregatorCategories;
  for (const CodegenAggregator& aggregator : aggregators_) {
    if (!impl_->categoryDirs.contains(aggregator.category))
      throw std::runtime_error("codegen registry: aggregator references unregistered category '" +
                               aggregator.category + "'");
    if (!aggregatorCategories.insert(aggregator.category).second)
      throw std::runtime_error("codegen registry: duplicate aggregator for category '" + aggregator.category + "'");
    // Aggregator templates are plain mustache files, not annotation-triggered
    // registry entries; they are only loaded here.
    loadTemplate(aggregator.templateName);
  }
}

void CodeGenerator::SetAstTree(AstTree* astTree) {
  astTree_ = astTree;
  InitMetaStructGroup();
}

void CodeGenerator::InitMetaStructGroup() {
  for (auto& metaStruct : astTree_->metaStructList) {
    if (hooks_.acceptType && !hooks_.acceptType(metaStruct))
      continue;
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
  if (hooks_.decorateTypeData)
    hooks_.decorateTypeData(*metaStruct, data);
  metaStructDataMap_[metaStruct] = std::move(data);
}

void CodeGenerator::Run() {
  PlanJobs();
  if (hooks_.transformJobs)
    hooks_.transformJobs(jobs_);
  RegisterOutputs();
  for (const auto& job : jobs_)
    RenderJob(job);
}

void CodeGenerator::PlanJobs() {
  jobs_.clear();
  impl_->outputsByCategory.clear();
  genFilesBySource_.clear();

  auto accepted = [this](const MetaStruct& metaStruct) {
    return !hooks_.acceptType || hooks_.acceptType(metaStruct);
  };

  // GlobalAggregate templates aggregate every annotated type of the whole
  // run into a single output file (e.g. enum_cast.gen.h). Registry order
  // keeps the job list deterministic.
  for (size_t i = 0; i < templates_.size(); i++) {
    const TemplateDesc& desc = templates_[i];
    if (desc.outputKind != OutputKind::GlobalAggregate)
      continue;
    if (!loadedTemplates_.contains(StringLib::ToLower(desc.name)))
      continue;  // template file missing (warned in Init)
    std::vector<MetaStruct*> matchedMetaStructList;
    for (auto& metaStruct : astTree_->metaStructList) {
      if (accepted(metaStruct) && GetAttribute(metaStruct.attributes, desc.name))
        matchedMetaStructList.push_back(&metaStruct);
    }
    if (!matchedMetaStructList.empty()) {
      jobs_.push_back({StringLib::ToLower(desc.name), desc.category, desc.outputKind, desc.outputPattern, "", "",
                       std::move(matchedMetaStructList)});
    }
  }

  // PerSourceFile templates emit one file per source file, aggregating the
  // types annotated with the template's name. metaStructList is ordered by
  // source file, so a group ends whenever the file changes.
  std::string filePath;
  std::string fileName;
  std::unordered_map<std::string, std::vector<MetaStruct*>> attributeMetaStructGroup;
  auto flushGroup = [&]() {
    for (auto& [attribute, metaStructList] : attributeMetaStructGroup) {
      auto indexIt = templateIndex_.find(attribute);
      if (indexIt == templateIndex_.end() || !loadedTemplates_.contains(attribute))
        continue;  // no such template, or its file is missing
      const TemplateDesc& desc = templates_[indexIt->second];
      if (desc.outputKind != OutputKind::PerSourceFile)
        continue;
      jobs_.push_back({attribute, desc.category, desc.outputKind, fileName + desc.outputPattern, fileName,
                       metaStructList.front()->sourceFilePath, metaStructList});
    }
    attributeMetaStructGroup.clear();
  };
  for (auto& metaStruct : astTree_->metaStructList) {
    if (filePath != metaStruct.sourceFilePath) {
      flushGroup();
      filePath = metaStruct.sourceFilePath;
      fileName = fs::path{filePath}.stem().string();
    }
    if (!accepted(metaStruct))
      continue;
    for (auto& attribute : metaStruct.attributes) {
      attributeMetaStructGroup[StringLib::ToLower(attribute)].push_back(&metaStruct);
    }
  }
  flushGroup();
}

void CodeGenerator::RegisterOutputs() {
  // Pre-register every planned output before rendering anything: the
  // gen_include_list of one file may reference outputs that are only
  // rendered later (e.g. a visit-ui file referencing the variant file).
  // Runs after hooks.transformJobs, so rewrites (and additions, provided
  // they reference registered templates) are honored everywhere.
  for (const GenJob& job : jobs_) {
    if (job.outputKind == OutputKind::CategoryAggregator)
      continue;  // aggregator jobs are appended below
    const TemplateDesc& desc = templates_.at(templateIndex_.at(job.templateName));
    impl_->outputsByCategory[job.category].push_back({desc.order, job.outputName});
    if (job.outputKind == OutputKind::PerSourceFile && !job.sourceFile.empty())
      genFilesBySource_[StringLib::NormalizePath(job.sourceFile)].push_back(
          {job.outputName, ResolveOutputDir(job.category).generic_string(), job.templateName});
  }

  // Deterministic include lists inside the aggregators.
  for (auto& [category, outputs] : impl_->outputsByCategory) {
    std::sort(outputs.begin(), outputs.end(), [](const auto& a, const auto& b) {
      if (a.first != b.first)
        return a.first < b.first;
      return a.second < b.second;
    });
  }

  // One aggregator job per registered aggregator whose category has outputs.
  for (const CodegenAggregator& aggregator : aggregators_) {
    auto outputsIt = impl_->outputsByCategory.find(aggregator.category);
    if (outputsIt == impl_->outputsByCategory.end() || outputsIt->second.empty())
      continue;
    const std::string templateKey = StringLib::ToLower(aggregator.templateName);
    if (!loadedTemplates_.contains(templateKey)) {
      std::cerr << "[CodeGenerator] warning: aggregator template not found, all_include skipped for category '"
                << aggregator.category << "': " << aggregator.templateName << std::endl;
      continue;
    }
    jobs_.push_back({templateKey, aggregator.category, OutputKind::CategoryAggregator, aggregator.outputName, "", "",
                     {}});
  }
}

void CodeGenerator::RenderJob(const GenJob& job) {
  // render() mutates the template's internal partials cache, hence no const.
  kainjow::mustache::mustache& tmpl = loadedTemplates_.at(job.templateName);
  const fs::path outDir = ResolveOutputDir(job.category);
  const std::string outDirString = outDir.generic_string();

  MustacheData data;
  if (job.outputKind == OutputKind::CategoryAggregator) {
    // The aggregator lives in the same directory as the files it lists, so
    // the bare file name is the correct relative spelling. The category
    // prelude comes first; per-file generated headers assume the aggregator
    // (or an equivalent prelude) is included before them.
    MustacheData includeFileList = MustacheData::type::list;
    if (auto preludeIt = config_.preludes.find(job.category); preludeIt != config_.preludes.end()) {
      for (const std::string& includedFile : preludeIt->second)
        includeFileList.push_back({"include_path", includedFile});
    }
    for (const auto& [order, outputName] : impl_->outputsByCategory.at(job.category))
      includeFileList.push_back({"include_path", "\"" + outputName + "\""});
    data.set("include_file_list", includeFileList);
  } else {
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

    // Generated outputs that must be visible when compiling this output,
    // filtered by template affinity (TemplateDesc::genIncludeDeps — a
    // template only sees its own outputs plus those of templates it
    // declares, e.g. visit-ui over variant):
    //   - same-source outputs of the visible templates (e.g. the variant
    //     file a visit-ui file operates on),
    //   - outputs of the visible templates for annotated headers the source
    //     file (transitively) includes, because generated code references
    //     their specializations — the `convert<Guid>` used by an emitted
    //     `as<Guid>()` lives in the Guid output of the same template, not in
    //     the (already included) Guid source header. Outputs of unrelated
    //     templates on the same headers are not referenced by this output's
    //     template and are left out to keep template families decoupled.
    MustacheData genIncludeList = MustacheData::type::list;
    if (!job.sourceFile.empty()) {
      std::unordered_set<std::string> visibleTemplates{job.templateName};
      if (auto descIt = templateIndex_.find(job.templateName); descIt != templateIndex_.end()) {
        for (const std::string& dep : templates_.at(descIt->second).genIncludeDeps)
          visibleTemplates.insert(StringLib::ToLower(dep));
      }
      const std::string normalizedSource = StringLib::NormalizePath(job.sourceFile);
      const std::string& self = job.outputName;
      std::set<std::string> genIncludes;
      auto collect = [&](const std::string& sourceKey) {
        if (auto it = genFilesBySource_.find(sourceKey); it != genFilesBySource_.end()) {
          for (const auto& ref : it->second) {
            if (!visibleTemplates.contains(ref.templateName))
              continue;
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
  }

  std::string result = tmpl.render(data);
  if (hooks_.transformOutput) {
    std::optional<std::string> transformed = hooks_.transformOutput(job, std::move(result));
    if (!transformed)
      return;  // the hook opted out of writing this output
    result = std::move(*transformed);
  }
  fs::create_directories(outDir);
  std::ofstream ofs{outDir / job.outputName, std::ios::binary};
  ofs << result << std::endl;
}

fs::path CodeGenerator::ResolveOutputDir(const std::string& category) const {
  const CategoryDirs& dirs = impl_->categoryDirs.at(category);
  return fs::path{config_.outputPath} / (config_.pathStyle == GenPathStyle::CamelCase ? dirs.camelDir : dirs.snakeDir);
}

bool CodeGenerator::GetAttribute(const std::vector<std::string>& attributes, const std::string& attribute) {
  return std::find_if(attributes.begin(), attributes.end(),
                      [&attribute](const std::string& item) { return StringLib::EqualsNoCase(item, attribute); }) !=
         attributes.end();
}
}  // namespace Aternyx
