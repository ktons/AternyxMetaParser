#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "Config/ArgConfig.h"

namespace fs = std::filesystem;

static void ResetArgConfig() {
  auto& config = ArgConfig::Instance();
  config.mode = Aternyx::CliMode::Codegen;
  config.projectPath_ = "";
  config.templatePath_ = "Template";
  config.outputPath_ = "_generated";
  config.sourceFile_ = "";
  config.includePaths_.clear();
  config.target_ = "";
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

TEST_F(ArgConfigTest, ValidateRequiresInput) {
  auto& config = ArgConfig::Instance();
  EXPECT_FALSE(config.Validate());
  config.sourceFile_ = "test_source.cpp";
  EXPECT_TRUE(config.Validate());
}

TEST_F(ArgConfigTest, ParseSourceFile) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser", "test_source.cpp"};
  int argc = 2;
  bool result = config.ParseArgs(argc, const_cast<char**>(argv));
  EXPECT_TRUE(result);
  EXPECT_EQ(config.sourceFile_, "test_source.cpp");
  EXPECT_EQ(config.mode, Aternyx::CliMode::Codegen);
}

TEST_F(ArgConfigTest, ParseNamedOptions) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser", "main.cpp",
                        "-o", "custom_output",
                        "-p", "custom_project",
                        "-t", "custom_templates"};
  int argc = 8;
  bool result = config.ParseArgs(argc, const_cast<char**>(argv));
  EXPECT_TRUE(result);
  EXPECT_EQ(config.sourceFile_, "main.cpp");
  EXPECT_EQ(config.outputPath_, "custom_output");
  EXPECT_EQ(config.projectPath_, "custom_project");
  EXPECT_EQ(config.templatePath_, "custom_templates");
}

TEST_F(ArgConfigTest, ParseIncludePath) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser", "main.cpp", "-i", "include_dir"};
  int argc = 4;
  bool result = config.ParseArgs(argc, const_cast<char**>(argv));
  EXPECT_TRUE(result);
}

TEST_F(ArgConfigTest, ParseNoSourceFile) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser"};
  int argc = 1;
  bool result = config.ParseArgs(argc, const_cast<char**>(argv));
  EXPECT_FALSE(result);
}

TEST_F(ArgConfigTest, CmakeModeSetsMode) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser", "--cmake", "build/compile_commands.json"};
  int argc = 3;
  EXPECT_TRUE(config.ParseArgs(argc, const_cast<char**>(argv)));
  EXPECT_EQ(config.mode, Aternyx::CliMode::CMake);
  EXPECT_EQ(config.sourceFile_, "build/compile_commands.json");
  EXPECT_TRUE(config.target_.empty());
}

TEST_F(ArgConfigTest, CodegenFlagExplicit) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser", "--codegen", "main.cpp"};
  int argc = 3;
  EXPECT_TRUE(config.ParseArgs(argc, const_cast<char**>(argv)));
  EXPECT_EQ(config.mode, Aternyx::CliMode::Codegen);
}

TEST_F(ArgConfigTest, MutuallyExclusiveModesRejected) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser", "--codegen", "--cmake", "main.cpp"};
  int argc = 4;
  EXPECT_FALSE(config.ParseArgs(argc, const_cast<char**>(argv)));
}

TEST_F(ArgConfigTest, ParseTarget) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser", "--cmake", "build/compile_commands.json",
                        "--target", "NyxCoreUtils", "-o", "out_dir"};
  int argc = 7;
  EXPECT_TRUE(config.ParseArgs(argc, const_cast<char**>(argv)));
  EXPECT_EQ(config.mode, Aternyx::CliMode::CMake);
  EXPECT_EQ(config.target_, "NyxCoreUtils");
  EXPECT_EQ(config.outputPath_, "out_dir");
}

TEST_F(ArgConfigTest, ParseGenPathStyleCamelCase) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser", "main.cpp", "--gen-path-style", "camel_case"};
  int argc = 4;
  EXPECT_TRUE(config.ParseArgs(argc, const_cast<char**>(argv)));
  EXPECT_EQ(config.genPathStyle_, Aternyx::GenPathStyle::CamelCase);
}

TEST_F(ArgConfigTest, ParseGenPathStyleInvalidRejected) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser", "main.cpp", "--gen-path-style", "kebab_case"};
  int argc = 4;
  EXPECT_FALSE(config.ParseArgs(argc, const_cast<char**>(argv)));
}

// The example TOML stores its keys inside a [parserParams] table; the parser
// must read nested and root-level keys alike.
TEST_F(ArgConfigTest, ParseTomlParserParamsTable) {
  auto& config = ArgConfig::Instance();
  fs::path exampleToml = fs::current_path() / "example" / "params.toml";
  ASSERT_TRUE(fs::exists(exampleToml)) << "example/params.toml missing (run from repo root)";

  EXPECT_TRUE(config.ParseTomlConfig(exampleToml.string().c_str()));
  ASSERT_FALSE(config.includePaths_.empty());
  EXPECT_EQ(config.includePaths_[0], "example");
}

TEST_F(ArgConfigTest, ParseTomlRootKeys) {
  auto& config = ArgConfig::Instance();
  fs::path tomlPath = CreateTempToml(R"toml(
output_path = "toml_out"
project_path = "toml_project"
template_path = "toml_template"
target = "SomeTarget"
gen_path_style = "camel_case"
)toml");

  EXPECT_TRUE(config.ParseTomlConfig(tomlPath.string().c_str()));
  EXPECT_EQ(config.outputPath_, "toml_out");
  EXPECT_EQ(config.projectPath_, "toml_project");
  EXPECT_EQ(config.templatePath_, "toml_template");
  EXPECT_EQ(config.target_, "SomeTarget");
  EXPECT_EQ(config.genPathStyle_, Aternyx::GenPathStyle::CamelCase);

  fs::remove(tomlPath);
}

// Explicitly provided CLI options must win over the TOML file, while TOML
// values win over built-in defaults.
TEST_F(ArgConfigTest, TomlYieldsToExplicitCliOptions) {
  auto& config = ArgConfig::Instance();
  fs::path tomlPath = CreateTempToml(R"toml(
output_path = "toml_out"
)toml");

  const std::string tomlArg = tomlPath.string();
  const char* argv[] = {"AternyxParser", "main.cpp", "--toml", tomlArg.c_str(), "-o", "cli_out"};
  int argc = 6;
  EXPECT_TRUE(config.ParseArgs(argc, const_cast<char**>(argv)));
  EXPECT_EQ(config.outputPath_, "cli_out");

  fs::remove(tomlPath);
}

TEST_F(ArgConfigTest, TomlKeptWhenCliOptionAbsent) {
  auto& config = ArgConfig::Instance();
  fs::path tomlPath = CreateTempToml(R"toml(
output_path = "toml_out"
)toml");

  const std::string tomlArg = tomlPath.string();
  const char* argv[] = {"AternyxParser", "main.cpp", "--toml", tomlArg.c_str()};
  int argc = 4;
  EXPECT_TRUE(config.ParseArgs(argc, const_cast<char**>(argv)));
  EXPECT_EQ(config.outputPath_, "toml_out");

  fs::remove(tomlPath);
}
