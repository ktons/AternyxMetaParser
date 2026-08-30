#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "CMakeAnalyzer/TargetCodegen.h"
#include "Parser/Parser.h"

namespace fs = std::filesystem;

namespace {

int g_tempCounter = 0;

fs::path CreateTempDir(const std::string& prefix) {
  fs::path dir = fs::temp_directory_path() / (prefix + std::to_string(++g_tempCounter));
  fs::create_directories(dir);
  return dir;
}

void WriteFile(const fs::path& path, const std::string& content) {
  std::ofstream ofs(path, std::ios::binary);
  ofs << content;
  ofs.close();
}

// Builds a compile db for `files` under one target and analyzes it.
Aternyx::CMake::CMakeTarget AnalyzeTarget(const fs::path& projectRoot, const fs::path& repoRoot,
                                          const std::vector<std::string>& files,
                                          const std::vector<std::string>& includePaths,
                                          const std::string& targetName = "miniproj") {
  std::string json = "[\n";
  for (size_t i = 0; i < files.size(); ++i) {
    json += "{";
    json += "\"directory\": \"" + projectRoot.generic_string() + "\", ";
    json += "\"command\": \"cl.exe -I" + projectRoot.generic_string() + " -I" + repoRoot.generic_string();
    for (const auto& extra : includePaths)
      json += " -I" + extra;
    json += " /FoSub/CMakeFiles/" + targetName + ".dir/" + std::to_string(i) + ".obj /c " + files[i] + "\", ";
    json += "\"file\": \"" + files[i] + "\", ";
    json += "\"output\": \"Sub/CMakeFiles/" + targetName + ".dir/" + std::to_string(i) + ".obj\"";
    json += "}";
    if (i + 1 < files.size())
      json += ",";
    json += "\n";
  }
  json += "]\n";

  fs::path dbDir = CreateTempDir("target_codegen_db_");
  WriteFile(dbDir / "compile_commands.json", json);

  Aternyx::CMake::CMakeTargetAnalyzer analyzer;
  if (!analyzer.Analyze(dbDir.string()))
    return {};
  const Aternyx::CMake::CMakeTarget* target = analyzer.FindTarget(targetName);
  return target != nullptr ? *target : Aternyx::CMake::CMakeTarget{};
}

}  // namespace

class TargetCodegenTest : public ::testing::Test {
 protected:
  void SetUp() override {
    repo_root_ = fs::current_path().string();
    fixture_root_ = fs::path(repo_root_) / "test" / "CMakeAnalyzer" / "fixtures" / "mini_project";
    ASSERT_TRUE(fs::exists(fixture_root_)) << "mini_project fixture missing (run from repo root)";

    output_dir_ = CreateTempDir("target_codegen_out_");
    if (fs::exists(output_dir_))
      fs::remove_all(output_dir_);
  }

  void TearDown() override {
    if (fs::exists(output_dir_))
      fs::remove_all(output_dir_);
  }

  Aternyx::CMake::TargetCodegenOptions DefaultOptions() const {
    Aternyx::CMake::TargetCodegenOptions options;
    options.outputPath = output_dir_.string();
    options.templatePath = (fs::path(repo_root_) / "Template").string();
    options.projectPath = repo_root_;
    options.pathStyle = Aternyx::GenPathStyle::SnakeCase;
    return options;
  }

  std::string repo_root_;
  fs::path fixture_root_;
  fs::path output_dir_;
};

// Both fixture .cpp files include the same annotated header; the merged
// generation must contain each convert specialization exactly once.
TEST_F(TargetCodegenTest, GeneratesOnceForSharedHeader) {
  auto target = AnalyzeTarget(fixture_root_, repo_root_, {"src/entity.cpp", "src/usage.cpp"}, {});
  ASSERT_EQ(target.name, "miniproj");
  ASSERT_EQ(target.cppFiles.size(), 2u);

  Aternyx::CMake::RunTargetCodegen(target, DefaultOptions());

  fs::path genFile = output_dir_ / "serialization" / "entity.gen.h";
  ASSERT_TRUE(fs::exists(genFile)) << "serialization/entity.gen.h was not generated";

  std::ifstream ifs(genFile, std::ios::binary);
  std::stringstream buffer;
  buffer << ifs.rdbuf();
  const std::string content = buffer.str();

  EXPECT_NE(content.find("convert<Mini::Entity>"), std::string::npos);
  EXPECT_NE(content.find("v.id"), std::string::npos);
  EXPECT_NE(content.find("v.name"), std::string::npos);

  // The shared header is parsed by two translation units: deduplication must
  // keep exactly one specialization.
  size_t occurrences = 0;
  for (size_t pos = content.find("struct convert<Mini::Entity>"); pos != std::string::npos;
       pos = content.find("struct convert<Mini::Entity>", pos + 1))
    ++occurrences;
  EXPECT_EQ(occurrences, 1u) << "Entity specialization duplicated";

  // The Component type only lives in the shared header too and must be there.
  EXPECT_NE(content.find("convert<Mini::Component>"), std::string::npos);

  EXPECT_TRUE(fs::exists(output_dir_ / "serialization" / "all_include.gen.h"));
}

// Empty targets and unknown templates must fail loudly.
TEST_F(TargetCodegenTest, ThrowsOnEmptyTarget) {
  Aternyx::CMake::CMakeTarget empty;
  empty.name = "ghost";
  EXPECT_THROW(Aternyx::CMake::RunTargetCodegen(empty, DefaultOptions()), std::runtime_error);
}

// A source that fails to parse (unresolved include) must abort generation
// with MetaParseError instead of silently producing broken code.
TEST_F(TargetCodegenTest, ThrowsOnParseDiagnostics) {
  fs::path brokenProject = CreateTempDir("target_codegen_broken_");
  WriteFile(brokenProject / "broken.cpp",
            "#include \"no_such_header_for_codegen_test.h\"\nint main() { return 0; }\n");

  auto target = AnalyzeTarget(brokenProject, repo_root_, {"broken.cpp"}, {});
  ASSERT_EQ(target.name, "miniproj");

  EXPECT_THROW(Aternyx::CMake::RunTargetCodegen(target, DefaultOptions()), Aternyx::MetaParseError);
}

// --parse-headers: the annotated header is parsed directly (no .cpp needed)
// and the generated include is spelled against the target's include roots,
// with the deepest root containing the header winning.
TEST_F(TargetCodegenTest, ParsesHeadersInsteadOfCppFiles) {
  auto target = AnalyzeTarget(fixture_root_, repo_root_, {"src/entity.cpp", "src/usage.cpp"}, {});
  ASSERT_EQ(target.name, "miniproj");

  Aternyx::CMake::TargetCodegenOptions options = DefaultOptions();
  options.projectPath = fixture_root_.string();
  options.parseHeaders = true;
  Aternyx::CMake::RunTargetCodegen(target, options);

  fs::path genFile = output_dir_ / "serialization" / "entity.gen.h";
  ASSERT_TRUE(fs::exists(genFile)) << "serialization/entity.gen.h was not generated";
  std::ifstream ifs(genFile, std::ios::binary);
  std::stringstream buffer;
  buffer << ifs.rdbuf();
  const std::string content = buffer.str();

  EXPECT_NE(content.find("convert<Mini::Entity>"), std::string::npos);
  // The target's include roots (-I fixture root, -I repo root) both contain
  // the header; the deeper one (fixture root) wins.
  EXPECT_NE(content.find("#include \"types/entity.h\""), std::string::npos);
  EXPECT_EQ(content.find('\\'), std::string::npos)
      << "backslashes must never appear in generated includes";
}

// A header that does not compile standalone must fail loudly in
// --parse-headers mode: headers must be self-contained and must not include
// generated files.
TEST_F(TargetCodegenTest, HeaderModeRejectsNonSelfContainedHeader) {
  fs::path badProject = CreateTempDir("target_codegen_badheader_");
  WriteFile(badProject / "bad.h",
            "#pragma once\n#include \"missing_include_xyz.h\"\nSTRUCT(Bad, Serialization) { META() int v; };\n");
  WriteFile(badProject / "tu.cpp", "#include \"bad.h\"\nint main() { return 0; }\n");

  auto target = AnalyzeTarget(badProject, repo_root_, {"tu.cpp"}, {});
  ASSERT_EQ(target.name, "miniproj");

  Aternyx::CMake::TargetCodegenOptions options = DefaultOptions();
  options.projectPath = badProject.string();
  options.parseHeaders = true;
  EXPECT_THROW(Aternyx::CMake::RunTargetCodegen(target, options), std::runtime_error);
}

// CamelCase style must route the generated files accordingly.
TEST_F(TargetCodegenTest, HonorsPathStyle) {
  auto target = AnalyzeTarget(fixture_root_, repo_root_, {"src/entity.cpp"}, {});
  ASSERT_EQ(target.name, "miniproj");

  Aternyx::CMake::TargetCodegenOptions options = DefaultOptions();
  options.pathStyle = Aternyx::GenPathStyle::CamelCase;
  Aternyx::CMake::RunTargetCodegen(target, options);

  EXPECT_TRUE(fs::exists(output_dir_ / "Serialization" / "entity.gen.h"));
}

// AstTree::MergeFrom: deduplication and derivedTypeIndex remapping.
TEST(AstTreeMergeTest, DedupesAndRemapsDerivedIndices) {
  Aternyx::AstTree treeA;
  {
    Aternyx::MetaStruct base;
    base.typeName = "Base";
    base.simpleTypeName = "Base";
    treeA.EmplaceBack(std::move(base));
  }
  {
    Aternyx::AstTree treeB;
    Aternyx::MetaStruct duplicateBase;  // same type as in treeA: skipped
    duplicateBase.typeName = "Base";
    duplicateBase.simpleTypeName = "Base";
    treeB.EmplaceBack(std::move(duplicateBase));

    Aternyx::MetaStruct derived;
    derived.typeName = "Derived";
    derived.simpleTypeName = "Derived";
    derived.baseTypeName = "Base";
    treeB.EmplaceBack(std::move(derived));  // index 1 in treeB

    Aternyx::MetaStruct unrelated;
    unrelated.typeName = "Other";
    unrelated.simpleTypeName = "Other";
    treeB.EmplaceBack(std::move(unrelated));  // index 2 in treeB

    treeA.MergeFrom(std::move(treeB));

    ASSERT_EQ(treeA.metaStructList.size(), 3u);  // Base, Derived, Other
    // Derived moved from index 1 to index 1 (Base occupied 0 and 0): its
    // own index is 1 here as well.
    EXPECT_EQ(treeA.metaStructList[0].typeName, "Base");
    EXPECT_EQ(treeA.metaStructList[1].typeName, "Derived");
    EXPECT_EQ(treeA.metaStructList[2].typeName, "Other");
    // Base must reference Derived exactly once.
    ASSERT_EQ(treeA.metaStructList[0].derivedTypeIndex.size(), 1u);
    EXPECT_EQ(treeA.metaStructList[0].derivedTypeIndex[0], 1u);
  }
}

// A base type collected in the first tree with its derived type only in the
// second tree must be linked by the merge.
TEST(AstTreeMergeTest, LinksCrossTreeBase) {
  Aternyx::AstTree treeA;
  {
    Aternyx::MetaStruct base;
    base.typeName = "Root";
    base.simpleTypeName = "Root";
    treeA.EmplaceBack(std::move(base));
  }

  Aternyx::AstTree treeB;
  {
    Aternyx::MetaStruct derived;
    derived.typeName = "Leaf";
    derived.simpleTypeName = "Leaf";
    derived.baseTypeName = "Root";
    treeB.EmplaceBack(std::move(derived));
  }

  treeA.MergeFrom(std::move(treeB));

  ASSERT_EQ(treeA.metaStructList.size(), 2u);
  ASSERT_EQ(treeA.metaStructList[0].typeName, "Root");
  ASSERT_EQ(treeA.metaStructList[1].typeName, "Leaf");
  ASSERT_EQ(treeA.metaStructList[0].derivedTypeIndex.size(), 1u);
  EXPECT_EQ(treeA.metaStructList[0].derivedTypeIndex[0], 1u);
}
