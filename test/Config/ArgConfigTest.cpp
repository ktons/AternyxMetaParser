#include <gtest/gtest.h>

#include "Config/ArgConfig.h"

static void ResetArgConfig() {
  auto& config = ArgConfig::Instance();
  config.projectPath_ = "";
  config.templatePath_ = "Template";
  config.outputPath_ = "_generated";
  config.sourceFile_ = "";
  config.includePaths_.clear();
}

class ArgConfigTest : public ::testing::Test {
 protected:
  void SetUp() override { ResetArgConfig(); }
};

TEST_F(ArgConfigTest, SingletonWorks) {
  auto& instance = ArgConfig::Instance();
  (void)instance;
}

TEST_F(ArgConfigTest, ValidateEmpty) {
  auto& config = ArgConfig::Instance();
  EXPECT_TRUE(config.Validate());
}

TEST_F(ArgConfigTest, ParseSourceFile) {
  auto& config = ArgConfig::Instance();
  const char* argv[] = {"AternyxParser", "test_source.cpp"};
  int argc = 2;
  bool result = config.ParseArgs(argc, const_cast<char**>(argv));
  EXPECT_TRUE(result);
  EXPECT_EQ(config.sourceFile_, "test_source.cpp");
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
