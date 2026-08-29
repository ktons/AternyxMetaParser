#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "Parser/Parser.h"

namespace fs = std::filesystem;

// Cross-platform temp file helper
static fs::path CreateTempFile(const std::string& content) {
  static int counter = 0;
  fs::path path = fs::temp_directory_path() / ("empty_test_" + std::to_string(++counter) + ".cpp");
  std::ofstream ofs(path);
  ofs << content;
  ofs.close();
  return path;
}

class MetaParserTest : public ::testing::Test {
 protected:
  std::string project_root_;
  std::string example_path_;
  std::string example_main_;

  void SetUp() override {
    project_root_ = fs::current_path().string();
    example_path_ = (fs::path(project_root_) / "example").string();
    example_main_ = (fs::path(project_root_) / "example" / "main.cpp").string();
  }

  Aternyx::AstTree ParseMain() {
    std::vector<std::string> include_paths = {example_path_};
    Aternyx::MetaParser parser(example_main_, include_paths);
    parser.BuildCursor();
    return std::move(parser.GetAstTree());
  }
};

TEST_F(MetaParserTest, ParseExampleFiles) {
  ASSERT_TRUE(fs::exists(example_main_));

  auto ast = ParseMain();

  ASSERT_FALSE(ast.metaStructList.empty());

  for (const auto& ms : ast.metaStructList) {
    std::cout << "  Found: " << ms.typeName
              << " (attrs: ";
    for (const auto& attr : ms.attributes)
      std::cout << attr << ",";
    std::cout << ")"
              << " fields: " << ms.fields.size();
    for (const auto& f : ms.fields)
      std::cout << " [" << f.name << ":" << f.type << "]";
    std::cout << std::endl;
  }

  bool found_annotated = false;
  for (const auto& ms : ast.metaStructList) {
    if (!ms.attributes.empty()) {
      found_annotated = true;
      break;
    }
  }
  EXPECT_TRUE(found_annotated);
}

TEST_F(MetaParserTest, VerifyClassAFields) {
  auto ast = ParseMain();

  bool found = false;
  for (const auto& ms : ast.metaStructList) {
    if (ms.simpleTypeName == "ClassA") {
      found = true;
      bool has_serialization = false;
      for (const auto& attr : ms.attributes) {
        if (attr == "Serialization") has_serialization = true;
      }
      EXPECT_TRUE(has_serialization);

      bool has_k = false, has_name = false, has_lengthList = false;
      for (const auto& f : ms.fields) {
        if (f.name == "k") has_k = true;
        if (f.name == "name") has_name = true;
        if (f.name == "lengthList_") has_lengthList = true;
      }
      EXPECT_TRUE(has_k);
      EXPECT_TRUE(has_name);
      EXPECT_TRUE(has_lengthList);
      break;
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(MetaParserTest, VerifyDataBlockFields) {
  auto ast = ParseMain();

  bool found = false;
  for (const auto& ms : ast.metaStructList) {
    if (ms.simpleTypeName == "DataBlock") {
      found = true;
      bool has_editor_ui = false;
      for (const auto& attr : ms.attributes) {
        if (attr == "EditorUI") has_editor_ui = true;
      }
      EXPECT_TRUE(has_editor_ui);

      bool has_a = false, has_b = false, has_name = false;
      for (const auto& f : ms.fields) {
        if (f.name == "a") has_a = true;
        if (f.name == "b") has_b = true;
        if (f.name == "name") has_name = true;
      }
      EXPECT_TRUE(has_a);
      EXPECT_TRUE(has_b);
      EXPECT_TRUE(has_name);
      break;
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(MetaParserTest, ParseEmptyFile) {
  fs::path tmpPath = CreateTempFile("int main() { return 0; }");

  std::vector<std::string> include_paths;
  Aternyx::MetaParser parser(tmpPath.string(), include_paths);
  EXPECT_NO_THROW(parser.BuildCursor());
  auto& ast = parser.GetAstTree();

  EXPECT_TRUE(ast.metaStructList.empty());

  fs::remove(tmpPath);
}

// An unresolved include must surface as a hard MetaParseError instead of
// letting clang error-recovery silently degrade field types to int.
TEST_F(MetaParserTest, MissingIncludeThrows) {
  fs::path tmpPath =
      CreateTempFile("#include \"no_such_header_for_diag_test.h\"\nint main() { return 0; }\n");

  std::vector<std::string> include_paths;
  Aternyx::MetaParser parser(tmpPath.string(), include_paths);
  EXPECT_THROW(parser.BuildCursor(), Aternyx::MetaParseError);

  fs::remove(tmpPath);
}

// extraClangArgs must replace the built-in -std flag (e.g. a compile db entry
// for a project built with a different language standard).
TEST_F(MetaParserTest, ExtraArgsOverrideStd) {
  fs::path tmpPath = CreateTempFile("int main() { return 0; }\n");

  std::vector<std::string> include_paths;
  std::vector<std::string> extra_args = {"-std=c++20", "-DMY_TEST_MACRO=1"};
  Aternyx::MetaParser parser(tmpPath.string(), include_paths, extra_args);
  EXPECT_NO_THROW(parser.BuildCursor());

  fs::remove(tmpPath);
}
