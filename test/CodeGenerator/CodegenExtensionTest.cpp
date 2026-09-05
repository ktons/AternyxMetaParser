#include <algorithm>
#include <clang-c/Index.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

#include "CodeGenerator/CodeGenerator.h"

namespace fs = std::filesystem;

namespace {

int g_dirCounter = 0;

fs::path CreateTempDir(const std::string& prefix) {
  fs::path dir = fs::temp_directory_path() / (prefix + std::to_string(++g_dirCounter));
  fs::create_directories(dir);
  return dir;
}

void WriteFile(const fs::path& path, const std::string& content) {
  std::ofstream ofs(path, std::ios::binary);
  ofs << content;
  ofs.close();
}

std::string ReadFileContent(const fs::path& path) {
  std::ifstream ifs(path, std::ios::binary);
  std::stringstream buffer;
  buffer << ifs.rdbuf();
  return buffer.str();
}

// A minimal annotated type: one property, attributes decide which templates
// pick it up. `sourceFile` groups per-file outputs.
Aternyx::MetaStruct MakeStruct(const std::string& typeName, const std::string& sourceFile,
                               const std::vector<std::string>& attributes) {
  Aternyx::MetaStruct metaStruct;
  metaStruct.kind = CXCursor_StructDecl;
  metaStruct.typeName = typeName;
  metaStruct.simpleTypeName = typeName;
  metaStruct.namespaceName = "";
  metaStruct.sourceFilePath = sourceFile;
  metaStruct.attributes = attributes;
  Aternyx::MetaField field;
  field.name = "value";
  field.type = "int";
  metaStruct.fields.push_back(field);
  return metaStruct;
}

}  // namespace

// Extension-surface tests for the opened-up codegen registry (categories,
// templates, aggregators, preludes) and the CodegenHooks behavior injection.
class CodegenExtensionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    template_dir_ = CreateTempDir("codegen_ext_tmpl_");
    output_dir_ = CreateTempDir("codegen_ext_out_");
  }

  void TearDown() override {
    fs::remove_all(template_dir_);
    fs::remove_all(output_dir_);
  }

  Aternyx::CodegenConfig BaseConfig() const {
    Aternyx::CodegenConfig config;
    config.outputPath = output_dir_.string();
    config.templatePath = template_dir_.string();
    return config;
  }

  // Drives the standard Init/SetAstTree/Run sequence; the tree must outlive
  // the generator.
  void Generate(Aternyx::CodegenConfig config, Aternyx::CodegenHooks hooks, Aternyx::AstTree& ast) {
    Aternyx::CodeGenerator generator;
    generator.Init(config, std::move(hooks));
    generator.SetAstTree(&ast);
    generator.Run();
  }

  fs::path template_dir_;
  fs::path output_dir_;
};

// A custom category with a custom template registered through the config
// alone: the built-in registry is replaced by a single PhysicsBind template.
TEST_F(CodegenExtensionTest, CustomCategoryWithCustomTemplate) {
  WriteFile(template_dir_ / "PhysicsBind.mustache",
            "// custom category output\n{{#meta_type_list}}{{type_name}} {{/meta_type_list}}\n");

  Aternyx::CodegenConfig config = BaseConfig();
  config.categories = Aternyx::DefaultCodegenCategories();
  config.categories.push_back({"physics", "physics", "Physics"});
  config.templates = {{"PhysicsBind", "physics", Aternyx::OutputKind::PerSourceFile, 100, "_bind.gen.h"}};

  Aternyx::AstTree ast;
  ast.metaStructList.push_back(MakeStruct("Thing", "/proj/thing.h", {"PhysicsBind"}));

  Generate(config, {}, ast);

  const fs::path bindFile = output_dir_ / "physics" / "thing_bind.gen.h";
  ASSERT_TRUE(fs::exists(bindFile)) << "custom category template output missing";
  EXPECT_NE(ReadFileContent(bindFile).find("Thing"), std::string::npos);
  EXPECT_FALSE(fs::exists(output_dir_ / "serialization")) << "replaced registry must not emit built-ins";
}

// Removing a built-in aggregator from the registry disables that category's
// all_include generation while the other categories keep theirs.
TEST_F(CodegenExtensionTest, RemovedAggregatorDisablesAllInclude) {
  const std::string sourceFile = (fs::temp_directory_path() / "codegen_ext_src_a.h").string();
  WriteFile(sourceFile, "#pragma once\n");

  Aternyx::CodegenConfig config = BaseConfig();
  config.templatePath = (fs::current_path() / "Template").string();
  config.includeRoots = {fs::temp_directory_path().string()};
  config.aggregators = Aternyx::DefaultAggregators();
  config.aggregators.erase(std::remove_if(config.aggregators.begin(), config.aggregators.end(),
                                          [](const Aternyx::CodegenAggregator& a) {
                                            return a.category == std::string(Aternyx::kCategoryEditorUi);
                                          }),
                           config.aggregators.end());

  Aternyx::AstTree ast;
  ast.metaStructList.push_back(MakeStruct("A", sourceFile, {"Serialization", "EditorUi"}));

  Generate(config, {}, ast);

  EXPECT_TRUE(fs::exists(output_dir_ / "serialization" / "all_include.gen.h"));
  EXPECT_TRUE(fs::exists(output_dir_ / "editor_ui" / "codegen_ext_src_a.gen.h"));
  EXPECT_FALSE(fs::exists(output_dir_ / "editor_ui" / "all_include.gen.h"))
      << "removing the aggregator must disable that category's all_include";
}

// preludes are data: the configured entries land at the top of the category's
// aggregator include list, before the planned outputs.
TEST_F(CodegenExtensionTest, PreludesAppearInAggregator) {
  const std::string sourceFile = (fs::temp_directory_path() / "codegen_ext_src_p.h").string();
  WriteFile(sourceFile, "#pragma once\n");

  Aternyx::CodegenConfig config = BaseConfig();
  config.templatePath = (fs::current_path() / "Template").string();
  config.includeRoots = {fs::temp_directory_path().string()};
  config.templates = Aternyx::DefaultTemplates();
  config.aggregators = Aternyx::DefaultAggregators();
  config.preludes[std::string(Aternyx::kCategorySerialization)] = {"<yaml-cpp/yaml.h>"};

  Aternyx::AstTree ast;
  ast.metaStructList.push_back(MakeStruct("P", sourceFile, {"Serialization"}));

  Generate(config, {}, ast);

  const std::string content = ReadFileContent(output_dir_ / "serialization" / "all_include.gen.h");
  EXPECT_NE(content.find("<yaml-cpp/yaml.h>"), std::string::npos) << "configured prelude missing";
  EXPECT_NE(content.find("#include \"codegen_ext_src_p.gen.h\""), std::string::npos)
      << "planned output missing from aggregator";
  EXPECT_LT(content.find("<yaml-cpp/yaml.h>"), content.find("codegen_ext_src_p.gen.h"))
      << "prelude must come before the outputs";

  // Without a configured prelude (the library default) no engine header is
  // injected — preludes are an upper-layer concern.
  Aternyx::CodegenConfig plainConfig = BaseConfig();
  plainConfig.templatePath = config.templatePath;
  plainConfig.includeRoots = config.includeRoots;
  Aternyx::AstTree ast2;
  ast2.metaStructList.push_back(MakeStruct("P", sourceFile, {"Serialization"}));
  Generate(plainConfig, {}, ast2);
  EXPECT_EQ(ReadFileContent(output_dir_ / "serialization" / "all_include.gen.h").find("yaml-cpp"),
            std::string::npos);
}

// A custom category can register its own aggregator; the prelude ordering
// rule applies there as well.
TEST_F(CodegenExtensionTest, CustomCategoryWithOwnAggregator) {
  WriteFile(template_dir_ / "PhysicsBind.mustache", "{{#meta_type_list}}{{type_name}} {{/meta_type_list}}\n");
  WriteFile(template_dir_ / "_AllInclude.mustache",
            "#pragma once\n{{#include_file_list}}#include {{include_path}}\n{{/include_file_list}}\n");
  const std::string sourceFile = (fs::temp_directory_path() / "codegen_ext_src_c.h").string();
  WriteFile(sourceFile, "#pragma once\n");

  Aternyx::CodegenConfig config = BaseConfig();
  config.categories = {{"physics", "physics", ""}};
  config.templates = {{"PhysicsBind", "physics", Aternyx::OutputKind::PerSourceFile, 100, "_bind.gen.h"}};
  config.aggregators = {{"physics", "_AllInclude", "all_include.gen.h"}};
  config.preludes["physics"] = {"<physics/kit.h>"};
  config.includeRoots = {fs::temp_directory_path().string()};

  Aternyx::AstTree ast;
  ast.metaStructList.push_back(MakeStruct("Thing", sourceFile, {"PhysicsBind"}));

  Generate(config, {}, ast);

  const std::string content = ReadFileContent(output_dir_ / "physics" / "all_include.gen.h");
  EXPECT_NE(content.find("<physics/kit.h>"), std::string::npos);
  EXPECT_NE(content.find("#include \"codegen_ext_src_c_bind.gen.h\""), std::string::npos);
}

// Registry validation: unknown categories and duplicate template names fail
// loudly in Init instead of misbehaving at render time.
TEST_F(CodegenExtensionTest, InitThrowsOnUnknownCategory) {
  Aternyx::CodegenConfig config = BaseConfig();
  config.templates = {{"X", "nosuch_category", Aternyx::OutputKind::PerSourceFile, 100, ".gen.h"}};
  Aternyx::CodeGenerator generator;
  EXPECT_THROW(generator.Init(config), std::runtime_error);
}

TEST_F(CodegenExtensionTest, InitThrowsOnDuplicateTemplateName) {
  Aternyx::CodegenConfig config = BaseConfig();
  config.templates = {{"Foo", "serialization", Aternyx::OutputKind::PerSourceFile, 100, ".gen.h"},
                      {"foo", "serialization", Aternyx::OutputKind::PerSourceFile, 100, "_2.gen.h"}};
  Aternyx::CodeGenerator generator;
  EXPECT_THROW(generator.Init(config), std::runtime_error);
}

// acceptType filters a type out of every stage: no job, no context, no
// aggregator entry for it.
TEST_F(CodegenExtensionTest, AcceptTypeHookFiltersTypes) {
  const std::string sourceFile = (fs::temp_directory_path() / "codegen_ext_src_f.h").string();
  WriteFile(sourceFile, "#pragma once\n");

  Aternyx::CodegenConfig config = BaseConfig();
  config.templatePath = (fs::current_path() / "Template").string();
  config.includeRoots = {fs::temp_directory_path().string()};

  Aternyx::AstTree ast;
  ast.metaStructList.push_back(MakeStruct("Keep", sourceFile, {"Serialization"}));
  ast.metaStructList.push_back(MakeStruct("Drop", sourceFile, {"Serialization"}));

  Aternyx::CodegenHooks hooks;
  hooks.acceptType = [](const Aternyx::MetaStruct& metaStruct) { return metaStruct.simpleTypeName != "Drop"; };

  Generate(config, std::move(hooks), ast);

  const std::string content = ReadFileContent(output_dir_ / "serialization" / "codegen_ext_src_f.gen.h");
  EXPECT_NE(content.find("Keep"), std::string::npos);
  EXPECT_EQ(content.find("Drop"), std::string::npos) << "filtered type must not be rendered";
}

// decorateTypeData injects computed fields into the mustache context — the
// escape hatch for "templates are not free enough".
TEST_F(CodegenExtensionTest, DecorateTypeDataAddsComputedFields) {
  WriteFile(template_dir_ / "Note.mustache",
            "// {{#meta_type_list}}{{custom_note}} {{/meta_type_list}}\n"
            "{{#meta_type_list}}{{type_name}}={{base_type_name}} {{/meta_type_list}}\n");
  WriteFile(template_dir_ / "_AllInclude.mustache",
            "#pragma once\n{{#include_file_list}}#include {{include_path}}\n{{/include_file_list}}\n");
  const std::string sourceFile = (fs::temp_directory_path() / "codegen_ext_src_n.h").string();
  WriteFile(sourceFile, "#pragma once\n");

  Aternyx::CodegenConfig config = BaseConfig();
  config.categories = {{"notes", "notes", ""}};
  config.templates = {{"Note", "notes", Aternyx::OutputKind::PerSourceFile, 100, ".gen.h"}};
  // Explicit: a replaced registry never silently keeps the default
  // aggregators (they would reference unregistered categories).
  config.aggregators = {{"notes", "_AllInclude", "all_include.gen.h"}};
  config.includeRoots = {fs::temp_directory_path().string()};

  Aternyx::AstTree ast;
  Aternyx::MetaStruct metaStruct = MakeStruct("Widget", sourceFile, {"Note"});
  metaStruct.baseTypeName = "Base::Widget";
  ast.metaStructList.push_back(metaStruct);

  Aternyx::CodegenHooks hooks;
  hooks.decorateTypeData = [](const Aternyx::MetaStruct& meta, kainjow::mustache::data& data) {
    data.set("custom_note", "note:" + meta.simpleTypeName);
  };

  Generate(config, std::move(hooks), ast);

  const std::string content = ReadFileContent(output_dir_ / "notes" / "codegen_ext_src_n.gen.h");
  EXPECT_NE(content.find("note:Widget"), std::string::npos);
  EXPECT_NE(content.find("Widget=Base::Widget"), std::string::npos) << "built-in fields must stay intact";
}

// transformJobs runs after planning and before output registration: a
// rewritten outputName flows into the written file AND the aggregator list.
TEST_F(CodegenExtensionTest, TransformJobsRewritesOutputName) {
  const std::string sourceFile = (fs::temp_directory_path() / "codegen_ext_src_t.h").string();
  WriteFile(sourceFile, "#pragma once\n");

  Aternyx::CodegenConfig config = BaseConfig();
  config.templatePath = (fs::current_path() / "Template").string();
  config.includeRoots = {fs::temp_directory_path().string()};

  Aternyx::AstTree ast;
  ast.metaStructList.push_back(MakeStruct("T", sourceFile, {"Serialization"}));

  Aternyx::CodegenHooks hooks;
  hooks.transformJobs = [](std::vector<Aternyx::GenJob>& jobs) {
    for (Aternyx::GenJob& job : jobs) {
      if (job.outputKind == Aternyx::OutputKind::PerSourceFile && job.category == std::string(Aternyx::kCategorySerialization))
        job.outputName = "renamed.gen.h";
    }
  };

  Generate(config, std::move(hooks), ast);

  EXPECT_TRUE(fs::exists(output_dir_ / "serialization" / "renamed.gen.h"));
  EXPECT_FALSE(fs::exists(output_dir_ / "serialization" / "codegen_ext_src_t.gen.h"))
      << "the original output name must be gone after the rewrite";
  EXPECT_NE(ReadFileContent(output_dir_ / "serialization" / "all_include.gen.h").find("renamed.gen.h"),
            std::string::npos)
      << "the aggregator must follow the rewritten output name";
}

// transformJobs can remove whole groups of jobs.
TEST_F(CodegenExtensionTest, TransformJobsRemovesCategory) {
  const std::string sourceFile = (fs::temp_directory_path() / "codegen_ext_src_r.h").string();
  WriteFile(sourceFile, "#pragma once\n");

  Aternyx::CodegenConfig config = BaseConfig();
  config.templatePath = (fs::current_path() / "Template").string();
  config.includeRoots = {fs::temp_directory_path().string()};

  Aternyx::AstTree ast;
  ast.metaStructList.push_back(MakeStruct("R", sourceFile, {"Serialization", "EditorUi"}));

  Aternyx::CodegenHooks hooks;
  hooks.transformJobs = [](std::vector<Aternyx::GenJob>& jobs) {
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                              [](const Aternyx::GenJob& job) {
                                return job.category == std::string(Aternyx::kCategoryEditorUi);
                              }),
               jobs.end());
  };

  Generate(config, std::move(hooks), ast);

  EXPECT_TRUE(fs::exists(output_dir_ / "serialization" / "codegen_ext_src_r.gen.h"));
  EXPECT_FALSE(fs::exists(output_dir_ / "editor_ui")) << "removed jobs must not be rendered";
}

// transformOutput rewrites content just before writing.
TEST_F(CodegenExtensionTest, TransformOutputRewritesContent) {
  const std::string sourceFile = (fs::temp_directory_path() / "codegen_ext_src_o.h").string();
  WriteFile(sourceFile, "#pragma once\n");

  Aternyx::CodegenConfig config = BaseConfig();
  config.templatePath = (fs::current_path() / "Template").string();
  config.includeRoots = {fs::temp_directory_path().string()};

  Aternyx::AstTree ast;
  ast.metaStructList.push_back(MakeStruct("O", sourceFile, {"Serialization"}));

  Aternyx::CodegenHooks hooks;
  hooks.transformOutput = [](const Aternyx::GenJob& job, std::string content) -> std::optional<std::string> {
    content += "// transformed: " + job.outputName + "\n";
    return content;
  };

  Generate(config, std::move(hooks), ast);

  const std::string content = ReadFileContent(output_dir_ / "serialization" / "codegen_ext_src_o.gen.h");
  EXPECT_NE(content.find("// transformed: codegen_ext_src_o.gen.h"), std::string::npos);
}

// transformOutput returning nullopt skips writing that output entirely.
TEST_F(CodegenExtensionTest, TransformOutputCanSkipWrites) {
  const std::string sourceFile = (fs::temp_directory_path() / "codegen_ext_src_s.h").string();
  WriteFile(sourceFile, "#pragma once\n");

  Aternyx::CodegenConfig config = BaseConfig();
  config.templatePath = (fs::current_path() / "Template").string();
  config.includeRoots = {fs::temp_directory_path().string()};

  Aternyx::AstTree ast;
  ast.metaStructList.push_back(MakeStruct("S", sourceFile, {"Serialization", "EditorUi"}));

  Aternyx::CodegenHooks hooks;
  hooks.transformOutput = [](const Aternyx::GenJob& job,
                             std::string content) -> std::optional<std::string> {
    if (job.outputKind == Aternyx::OutputKind::CategoryAggregator ||
        job.category == std::string(Aternyx::kCategoryEditorUi))
      return std::nullopt;
    return content;
  };

  Generate(config, std::move(hooks), ast);

  EXPECT_TRUE(fs::exists(output_dir_ / "serialization" / "codegen_ext_src_s.gen.h"));
  EXPECT_FALSE(fs::exists(output_dir_ / "editor_ui" / "codegen_ext_src_s.gen.h"));
  EXPECT_FALSE(fs::exists(output_dir_ / "serialization" / "all_include.gen.h"))
      << "a skipped aggregator output must not be written";
}
