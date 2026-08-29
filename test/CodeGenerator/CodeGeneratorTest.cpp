#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "CodeGenerator/CodeGenerator.h"
#include "Config/ArgConfig.h"
#include "Parser/Parser.h"

namespace fs = std::filesystem;

// Helper: fully reset ArgConfig singleton to fresh defaults
static void ResetArgConfig() {
  auto& config = ArgConfig::Instance();
  config.projectPath_ = "";
  config.templatePath_ = "Template";
  config.outputPath_ = "_generated";
  config.sourceFile_ = "";
  config.includePaths_.clear();
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
    ResetArgConfig();

    project_root_ = fs::current_path().string();
    example_path_ = (fs::path(project_root_) / "example").string();
    template_path_ = (fs::path(project_root_) / "Template").string();
    output_path_ = (fs::path(project_root_) / "_generated_test").string();
    example_main_ = (fs::path(project_root_) / "example" / "main.cpp").string();

    if (fs::exists(output_path_)) {
      fs::remove_all(output_path_);
    }

    auto& config = ArgConfig::Instance();
    config.projectPath_ = project_root_;
    config.templatePath_ = template_path_;
    config.outputPath_ = output_path_;
  }

  void TearDown() override {
    if (fs::exists(output_path_)) {
      fs::remove_all(output_path_);
    }
  }
};

TEST_F(CodeGeneratorTest, Init) {
  Aternyx::CodeGenerator generator;
  EXPECT_NO_THROW(generator.Init());
}

TEST_F(CodeGeneratorTest, ParseAndGenerate) {
  ASSERT_TRUE(fs::exists(example_main_));

  std::vector<std::string> include_paths = {example_path_};
  Aternyx::MetaParser parser(example_main_, include_paths);
  parser.BuildCursor();
  auto& ast = parser.GetAstTree();

  ASSERT_FALSE(ast.metaStructList.empty());

  Aternyx::CodeGenerator generator;
  generator.Init();
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
  generator.Init();
  generator.SetAstTree(&empty_ast);
  EXPECT_NO_THROW(generator.Run());
}

// Check that output files are generated and non-empty
TEST_F(CodeGeneratorTest, VerifyGeneratedFilesExist) {
  std::vector<std::string> include_paths = {example_path_};
  Aternyx::MetaParser parser(example_main_, include_paths);
  parser.BuildCursor();
  auto& ast = parser.GetAstTree();

  ASSERT_FALSE(ast.metaStructList.empty());

  Aternyx::CodeGenerator generator;
  generator.Init();
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
  EXPECT_GE(h_file_count, 2)
      << "Expected at least 2 .h files (all_include + user_struct), found " << h_file_count;
}

// Golden content check: every annotated field of the example types must appear
// in the generated yaml-cpp convert specializations with its exact name and type.
TEST_F(CodeGeneratorTest, VerifySerializationContent) {
  std::vector<std::string> include_paths = {example_path_};
  Aternyx::MetaParser parser(example_main_, include_paths);
  parser.BuildCursor();
  auto& ast = parser.GetAstTree();
  ASSERT_FALSE(ast.metaStructList.empty());

  Aternyx::CodeGenerator generator;
  generator.Init();
  generator.SetAstTree(&ast);
  generator.Run();

  std::string serialization_file =
      (fs::path(output_path_) / "serialization" / "user_struct.gen.h").string();
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
  EXPECT_EQ(content.find("node[\"scratch\"]"), std::string::npos)
      << "Runtime field 'scratch' must not be serialized";

  // Multi-attribute annotation "Serialization, EditorUI" must also hit the
  // EditorUi template (case-insensitive, whitespace-trimmed attribute match).
  std::string editor_ui_file = (fs::path(output_path_) / "editor_ui" / "user_struct.gen.h").string();
  ASSERT_TRUE(fs::exists(editor_ui_file)) << "editor_ui/user_struct.gen.h was not generated";
  const std::string ui_content = ReadFileContent(editor_ui_file);
  EXPECT_NE(ui_content.find("ImGuiUtilityInternal<UserStruct::DataBlock>"), std::string::npos);
  EXPECT_NE(ui_content.find("OnEditGui(\"a\", v.a)"), std::string::npos);
  EXPECT_NE(ui_content.find("OnEditGui(\"name\", v.name)"), std::string::npos);
}
