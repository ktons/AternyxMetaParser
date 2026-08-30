#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "Config/ArgConfig.h"

namespace fs = std::filesystem;

static void ResetArgConfig() {
  auto& config = ArgConfig::Instance();
  config.projectPath_ = "";
  config.templatePath_ = "Template";
  config.outputPath_ = "_generated";
  config.compileDbPath_ = "";
  config.includePaths_.clear();
  config.target_ = "";
  config.parseHeaders_ = false;
  config.headerMarkers_.clear();
  config.genPathStyle_ = Aternyx::GenPathStyle::SnakeCase;
}

class ArgConfigTest : public ::testing::Test {
 protected:
  void SetUp() override { ResetArgConfig(); }

  static fs::path CreateTempToml(const std::string& content) {
    static int counter = 0;
    fs::path path = fs::temp_directory_path() / ("argconfig_test_" + std::to_string(++counter) + ".toml");
    std::ofstream ofs(path);
    ofs << content;
    ofs.close();
    return path;
  }
};

TEST_F(ArgConfigTest, SingletonWorks) {
  auto& instance = ArgConfig::Instance();
  (void)instance;
}

TEST_F(ArgConfigTest, ValidateRequiresCompileDb) {
  auto& config = ArgConfig::Instance();
  EXPECT_FALSE(config.Validate());
  config.compileDbPath_ = "build/compile_commands.json";
  EXPECT_TRUE(config.Validate());
}

TEST_F(ArgConfigTest, ParseCompileDbPath) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser", "build/compile_commands.json"};
  int argc = 2;
  bool result = config.ParseArgs(argc, const_cast<char**>(argv));
  EXPECT_TRUE(result);
  EXPECT_EQ(config.compileDbPath_, "build/compile_commands.json");
  EXPECT_TRUE(config.target_.empty());
}

TEST_F(ArgConfigTest, ParseNamedOptions) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser", "build/compile_commands.json",
                        "-o", "custom_output",
                        "-p", "custom_project",
                        "-t", "custom_templates"};
  int argc = 8;
  bool result = config.ParseArgs(argc, const_cast<char**>(argv));
  EXPECT_TRUE(result);
  EXPECT_EQ(config.compileDbPath_, "build/compile_commands.json");
  EXPECT_EQ(config.outputPath_, "custom_output");
  EXPECT_EQ(config.projectPath_, "custom_project");
  EXPECT_EQ(config.templatePath_, "custom_templates");
}

TEST_F(ArgConfigTest, ParseIncludePath) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser", "build/compile_commands.json", "-i", "include_dir"};
  int argc = 4;
  bool result = config.ParseArgs(argc, const_cast<char**>(argv));
  EXPECT_TRUE(result);
  ASSERT_FALSE(config.includePaths_.empty());
  EXPECT_EQ(config.includePaths_[0], "include_dir");
}

TEST_F(ArgConfigTest, ParseNoInput) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser"};
  int argc = 1;
  bool result = config.ParseArgs(argc, const_cast<char**>(argv));
  EXPECT_FALSE(result);
}

TEST_F(ArgConfigTest, ParseTarget) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser", "build/compile_commands.json",
                        "--target", "NyxCoreUtils", "-o", "out_dir"};
  int argc = 6;
  EXPECT_TRUE(config.ParseArgs(argc, const_cast<char**>(argv)));
  EXPECT_EQ(config.target_, "NyxCoreUtils");
  EXPECT_EQ(config.outputPath_, "out_dir");
}

TEST_F(ArgConfigTest, ParseTomlStyle) {
  auto& config = ArgConfig::Instance();
  fs::path tomlPath = CreateTempToml(R"toml(
gen_path_style = "camel_case"
)toml");

  EXPECT_TRUE(config.ParseTomlConfig(tomlPath.string().c_str()));
  EXPECT_EQ(config.genPathStyle_, Aternyx::GenPathStyle::CamelCase);

  fs::remove(tomlPath);
}

TEST_F(ArgConfigTest, ParseTomlParseHeadersAndMarkers) {
  auto& config = ArgConfig::Instance();
  fs::path tomlPath = CreateTempToml(R"toml(
parse_headers = true
header_markers = ["REFLECT(", "SERIALIZE("]
)toml");

  EXPECT_TRUE(config.ParseTomlConfig(tomlPath.string().c_str()));
  EXPECT_TRUE(config.parseHeaders_);
  ASSERT_EQ(config.headerMarkers_.size(), 2u);
  EXPECT_EQ(config.headerMarkers_[0], "REFLECT(");
  EXPECT_EQ(config.headerMarkers_[1], "SERIALIZE(");

  fs::remove(tomlPath);
}

TEST_F(ArgConfigTest, ParseTomlInvalidStyleRejected) {
  auto& config = ArgConfig::Instance();
  fs::path tomlPath = CreateTempToml(R"toml(
gen_path_style = "kebab_case"
)toml");

  EXPECT_FALSE(config.ParseTomlConfig(tomlPath.string().c_str()));

  // The same rejection must fail the whole argument parse.
  const std::string tomlArg = tomlPath.string();
  const char* argv[] = {"AternyxParser", "build/compile_commands.json", "--toml", tomlArg.c_str()};
  int argc = 4;
  EXPECT_FALSE(config.ParseArgs(argc, const_cast<char**>(argv)));

  fs::remove(tomlPath);
}

// Unknown keys and absent keys are ignored: the built-in defaults survive.
TEST_F(ArgConfigTest, ParseTomlDefaultsKeptWhenKeysAbsent) {
  auto& config = ArgConfig::Instance();
  fs::path tomlPath = CreateTempToml(R"toml(
template_path = "ignored"
output_path = "ignored"
)toml");

  EXPECT_TRUE(config.ParseTomlConfig(tomlPath.string().c_str()));
  EXPECT_FALSE(config.parseHeaders_);
  EXPECT_TRUE(config.headerMarkers_.empty());
  EXPECT_EQ(config.genPathStyle_, Aternyx::GenPathStyle::SnakeCase);

  fs::remove(tomlPath);
}

// The shipped example TOML uses the documented root-level schema.
TEST_F(ArgConfigTest, ParseTomlExampleFile) {
  auto& config = ArgConfig::Instance();
  fs::path exampleToml = fs::current_path() / "example" / "params.toml";
  ASSERT_TRUE(fs::exists(exampleToml)) << "example/params.toml missing (run from repo root)";

  EXPECT_TRUE(config.ParseTomlConfig(exampleToml.string().c_str()));
  EXPECT_TRUE(config.parseHeaders_);
  ASSERT_EQ(config.headerMarkers_.size(), 3u);
  EXPECT_EQ(config.headerMarkers_[0], "CLASS(");
  EXPECT_EQ(config.genPathStyle_, Aternyx::GenPathStyle::SnakeCase);
}
