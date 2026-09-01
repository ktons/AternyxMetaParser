#include <clang-c/Index.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

#include "CodeGenerator/CodeGenerator.h"
#include "Parser/Parser.h"

namespace fs = std::filesystem;

// Build an explicit CodegenConfig (CodeGenerator no longer reads the global
// ArgConfig singleton).
static Aternyx::CodegenConfig MakeConfig(const std::string& outputPath,
                                         const std::string& templatePath,
                                         Aternyx::GenPathStyle style = Aternyx::GenPathStyle::SnakeCase,
                                         const std::vector<std::string>& includeRoots = {}) {
  Aternyx::CodegenConfig config;
  config.outputPath = outputPath;
  config.templatePath = templatePath;
  config.pathStyle = style;
  config.includeRoots = includeRoots;
  return config;
}

static std::string ReadFileContent(const std::string& path) {
  std::ifstream ifs(path);
  std::stringstream buffer;
  buffer << ifs.rdbuf();
  return buffer.str();
}

class CodeGeneratorTest : public ::testing::Test {
 protected:
  std::string project_root_;
  std::string example_path_;
  std::string template_path_;
  std::string output_path_;
  std::string example_main_;

  void SetUp() override {
    project_root_ = fs::current_path().string();
    example_path_ = (fs::path(project_root_) / "example").string();
    template_path_ = (fs::path(project_root_) / "Template").string();
    output_path_ = (fs::path(project_root_) / "_generated_test").string();
    example_main_ = (fs::path(project_root_) / "example" / "main.cpp").string();

    if (fs::exists(output_path_)) {
      fs::remove_all(output_path_);
    }
  }

  void TearDown() override {
    if (fs::exists(output_path_)) {
      fs::remove_all(output_path_);
    }
  }

  Aternyx::AstTree ParseExample() {
    std::vector<std::string> include_paths = {example_path_};
    Aternyx::MetaParser parser(example_main_, include_paths);
    parser.BuildCursor();
    return std::move(parser.GetAstTree());
  }
};

TEST_F(CodeGeneratorTest, Init) {
  Aternyx::CodeGenerator generator;
  EXPECT_NO_THROW(generator.Init(MakeConfig(output_path_, template_path_)));
}

TEST_F(CodeGeneratorTest, ParseAndGenerate) {
  ASSERT_TRUE(fs::exists(example_main_));

  auto ast = ParseExample();
  ASSERT_FALSE(ast.metaStructList.empty());

  Aternyx::CodeGenerator generator;
  generator.Init(MakeConfig(output_path_, template_path_));
  generator.SetAstTree(&ast);
  EXPECT_NO_THROW(generator.Run());

  EXPECT_TRUE(fs::exists(output_path_));

  std::string serialization_path = (fs::path(output_path_) / "serialization").string();
  if (fs::exists(serialization_path)) {
    bool found_gen_file = false;
    for (const auto& entry : fs::directory_iterator(serialization_path)) {
      if (entry.path().extension() == ".h") {
        found_gen_file = true;
        std::cout << "  Generated: " << entry.path().filename() << std::endl;
      }
    }
    EXPECT_TRUE(found_gen_file);
  }
}

TEST_F(CodeGeneratorTest, GenerateEmptyAst) {
  Aternyx::AstTree empty_ast;
  Aternyx::CodeGenerator generator;
  generator.Init(MakeConfig(output_path_, template_path_));
  generator.SetAstTree(&empty_ast);
  EXPECT_NO_THROW(generator.Run());
}

// Check that output files are generated and non-empty
TEST_F(CodeGeneratorTest, VerifyGeneratedFilesExist) {
  auto ast = ParseExample();
  ASSERT_FALSE(ast.metaStructList.empty());

  Aternyx::CodeGenerator generator;
  generator.Init(MakeConfig(output_path_, template_path_));
  generator.SetAstTree(&ast);
  generator.Run();

  std::string serialization_path = (fs::path(output_path_) / "serialization").string();
  ASSERT_TRUE(fs::exists(serialization_path));

  int h_file_count = 0;
  for (const auto& entry : fs::directory_iterator(serialization_path)) {
    if (entry.path().extension() == ".h") {
      h_file_count++;
      auto file_size = fs::file_size(entry.path());
      EXPECT_GT(file_size, 0) << "Generated file is empty: " << entry.path().filename();
      std::cout << "  Found: " << entry.path().filename() << " (" << file_size << " bytes)" << std::endl;
    }
  }
  EXPECT_GE(h_file_count, 2) << "Expected at least 2 .h files (all_include + user_struct), found " << h_file_count;
}

// Golden content check: every annotated field of the example types must appear
// in the generated yaml-cpp convert specializations with its exact name and type.
TEST_F(CodeGeneratorTest, VerifySerializationContent) {
  auto ast = ParseExample();
  ASSERT_FALSE(ast.metaStructList.empty());

  Aternyx::CodeGenerator generator;
  generator.Init(MakeConfig(output_path_, template_path_));
  generator.SetAstTree(&ast);
  generator.Run();

  std::string serialization_file = (fs::path(output_path_) / "serialization" / "user_struct.gen.h").string();
  ASSERT_TRUE(fs::exists(serialization_file)) << "serialization/user_struct.gen.h was not generated";
  const std::string content = ReadFileContent(serialization_file);

  EXPECT_NE(content.find("convert<UserStruct::ClassA>"), std::string::npos)
      << "ClassA specialization missing, typeName spelling was not namespace-qualified";
  EXPECT_NE(content.find("convert<UserStruct::DataBlock>"), std::string::npos)
      << "DataBlock specialization missing (last annotated type lost?)";

  // ClassA fields
  EXPECT_NE(content.find("node[\"k\"]"), std::string::npos);
  EXPECT_NE(content.find("node[\"name\"]"), std::string::npos);
  EXPECT_NE(content.find("node[\"lengthList_\"]"), std::string::npos);
  // DataBlock fields
  EXPECT_NE(content.find("node[\"a\"]"), std::string::npos);
  EXPECT_NE(content.find("node[\"b\"]"), std::string::npos);

  // decode type mapping
  EXPECT_NE(content.find("as<int>()"), std::string::npos);
  EXPECT_NE(content.find("as<std::string>()"), std::string::npos);
  EXPECT_NE(content.find("std::vector<float>"), std::string::npos);

  // Runtime-marked field must be excluded from serialization
  EXPECT_EQ(content.find("node[\"scratch\"]"), std::string::npos) << "Runtime field 'scratch' must not be serialized";

  // Multi-attribute annotation "Serialization, EditorUI" must also hit the
  // EditorUi template (case-insensitive, whitespace-trimmed attribute match).
  std::string editor_ui_file = (fs::path(output_path_) / "editor_ui" / "user_struct.gen.h").string();
  ASSERT_TRUE(fs::exists(editor_ui_file)) << "editor_ui/user_struct.gen.h was not generated";
  const std::string ui_content = ReadFileContent(editor_ui_file);
  EXPECT_NE(ui_content.find("ImGuiUtilityInternal<UserStruct::DataBlock>"), std::string::npos);
  EXPECT_NE(ui_content.find("OnEditGui(\"a\", v.a)"), std::string::npos);
  EXPECT_NE(ui_content.find("OnEditGui(\"name\", v.name)"), std::string::npos);
}

// The include spelling for the analyzed header is derived from the include
// roots: the deepest root containing the header wins, and the spelling is
// relative to it.
TEST_F(CodeGeneratorTest, IncludeSpellingUsesDeepestRoot) {
  auto ast = ParseExample();
  ASSERT_FALSE(ast.metaStructList.empty());

  Aternyx::CodeGenerator generator;
  generator.Init(
      MakeConfig(output_path_, template_path_, Aternyx::GenPathStyle::SnakeCase, {example_path_, project_root_}));
  generator.SetAstTree(&ast);
  generator.Run();

  const std::string content =
      ReadFileContent((fs::path(output_path_) / "serialization" / "user_struct.gen.h").string());
  // Both roots contain example/user_struct.h; the deeper one (example/) wins.
  EXPECT_NE(content.find("#include \"user_struct.h\""), std::string::npos);
  EXPECT_EQ(content.find("example/user_struct.h"), std::string::npos) << "the shallower root must not win the spelling";
}

// Without any include roots the include falls back to a path relative to the
// generated file's own directory (resolvable through the compiler's
// quoted-include "directory of the including file first" rule) and always
// uses forward slashes.
TEST_F(CodeGeneratorTest, IncludeSpellingFallsBackRelativeToOutput) {
  auto ast = ParseExample();
  ASSERT_FALSE(ast.metaStructList.empty());

  Aternyx::CodeGenerator generator;
  generator.Init(MakeConfig(output_path_, template_path_));
  generator.SetAstTree(&ast);
  generator.Run();

  const std::string content =
      ReadFileContent((fs::path(output_path_) / "serialization" / "user_struct.gen.h").string());
  EXPECT_NE(content.find("#include \"../../example/user_struct.h\""), std::string::npos);
  EXPECT_EQ(content.find('\\'), std::string::npos) << "backslashes must never appear in generated includes";
}

// Generated files must not hard-code output-root-relative paths (the old
// VisitEditorUi defect) nor contain backslashes.
TEST_F(CodeGeneratorTest, NoHardcodedGeneratedIncludePaths) {
  auto ast = ParseExample();
  ASSERT_FALSE(ast.metaStructList.empty());

  Aternyx::CodeGenerator generator;
  generator.Init(MakeConfig(output_path_, template_path_));
  generator.SetAstTree(&ast);
  generator.Run();

  ASSERT_TRUE(fs::exists(output_path_));
  for (const auto& entry : fs::recursive_directory_iterator(output_path_)) {
    if (entry.path().extension() != ".h")
      continue;
    const std::string content = ReadFileContent(entry.path().string());
    EXPECT_EQ(content.find("_generated/"), std::string::npos)
        << "hard-coded generated path in " << entry.path().filename().string();
    EXPECT_EQ(content.find('\\'), std::string::npos) << "backslash in " << entry.path().filename().string();
  }
}

// gen_include_list: a per-file output references the sibling outputs derived
// from the same source file, spelled relative to its own directory.
TEST_F(CodeGeneratorTest, GenIncludeListSpellsSiblingOutputs) {
  const std::string sourceFile = (fs::path(project_root_) / "example" / "base.h").string();

  Aternyx::AstTree ast;
  Aternyx::MetaStruct base;
  base.kind = CXCursor_StructDecl;
  base.typeName = "Mini::Root";
  base.simpleTypeName = "Root";
  base.sourceFilePath = sourceFile;
  base.attributes = {"Variant", "VisitEditorUi"};
  base.derivedTypeIndex = {1u};
  ast.metaStructList.push_back(std::move(base));

  Aternyx::MetaStruct derived;
  derived.kind = CXCursor_StructDecl;
  derived.typeName = "Mini::Leaf";
  derived.simpleTypeName = "Leaf";
  derived.sourceFilePath = sourceFile;
  derived.baseTypeName = "Mini::Root";
  ast.metaStructList.push_back(std::move(derived));

  Aternyx::CodeGenerator generator;
  generator.Init(MakeConfig(output_path_,
                            template_path_,
                            Aternyx::GenPathStyle::SnakeCase,
                            {(fs::path(project_root_) / "example").string()}));
  generator.SetAstTree(&ast);
  generator.Run();

  // The visit-ui file must reference the variant file of the same source,
  // relative to its own directory — never through a hard-coded output root.
  const std::string visitContent =
      ReadFileContent((fs::path(output_path_) / "editor_ui" / "base_visit_ui.gen.h").string());
  EXPECT_NE(visitContent.find("#include \"../reflection/base_variant.gen.h\""), std::string::npos);
  EXPECT_EQ(visitContent.find("_generated/"), std::string::npos);

  // The variant file is generated next to it; the visit-ui file reached it
  // without any hard-coded output root.
  const fs::path variantFile = fs::path(output_path_) / "reflection" / "base_variant.gen.h";
  ASSERT_TRUE(fs::exists(variantFile));
  const std::string variantContent = ReadFileContent(variantFile.string());
  EXPECT_EQ(variantContent.find("_generated/"), std::string::npos);
  EXPECT_EQ(variantContent.find('\\'), std::string::npos);
}

// The path style option must rename the generated sub-directories while the
// default (snake_case) keeps the historical layout.
TEST_F(CodeGeneratorTest, GenPathStyleCamelCase) {
  auto ast = ParseExample();
  ASSERT_FALSE(ast.metaStructList.empty());

  Aternyx::CodeGenerator generator;
  generator.Init(MakeConfig(output_path_, template_path_, Aternyx::GenPathStyle::CamelCase));
  generator.SetAstTree(&ast);
  generator.Run();

  const std::string camel_serialization = (fs::path(output_path_) / "Serialization" / "user_struct.gen.h").string();
  ASSERT_TRUE(fs::exists(camel_serialization)) << "Serialization/user_struct.gen.h was not generated";
  EXPECT_NE(ReadFileContent(camel_serialization).find("convert<UserStruct::ClassA>"), std::string::npos);

  const std::string camel_editor_ui = (fs::path(output_path_) / "EditorUi" / "user_struct.gen.h").string();
  ASSERT_TRUE(fs::exists(camel_editor_ui)) << "EditorUi/user_struct.gen.h was not generated";
}

TEST_F(CodeGeneratorTest, ParseGenPathStyleNames) {
  Aternyx::GenPathStyle style = Aternyx::GenPathStyle::SnakeCase;
  EXPECT_TRUE(Aternyx::ParseGenPathStyle("snake_case", style));
  EXPECT_EQ(style, Aternyx::GenPathStyle::SnakeCase);
  EXPECT_TRUE(Aternyx::ParseGenPathStyle("CamelCase", style));
  EXPECT_EQ(style, Aternyx::GenPathStyle::CamelCase);
  EXPECT_FALSE(Aternyx::ParseGenPathStyle("kebab-case", style));
}
