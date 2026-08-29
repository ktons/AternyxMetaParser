#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "CMakeAnalyzer/CMakeTargetAnalyzer.h"

namespace fs = std::filesystem;

namespace {

int g_tempCounter = 0;

// Creates a temp directory holding a compile db with %TEMP_DIR% replaced by
// the real (forward-slash) path, then analyzes it.
bool AnalyzeDb(const std::string& jsonText, Aternyx::CMake::CMakeTargetAnalyzer& analyzer,
               fs::path* outDir = nullptr) {
  fs::path dir = fs::temp_directory_path() / ("cmake_target_analyzer_test_" + std::to_string(++g_tempCounter));
  fs::create_directories(dir);

  std::string content = jsonText;
  const std::string token = "%TEMP_DIR%";
  const std::string replacement = dir.generic_string();
  size_t position = content.find(token);
  while (position != std::string::npos) {
    content.replace(position, token.size(), replacement);
    position = content.find(token, position + replacement.size());
  }
  std::ofstream ofs(dir / "compile_commands.json", std::ios::binary);
  ofs << content;
  ofs.close();

  if (outDir != nullptr)
    *outDir = dir;
  return analyzer.Analyze(dir.string());
}

}  // namespace

// Mirrors the CMake+Ninja+MSVC layout observed in real projects: dash-style
// -I, slash-style /D and dash-colon -std:c++17 in the same command.
TEST(CMakeTargetAnalyzerTest, MixedMsvcStyleFlags) {
  Aternyx::CMake::CMakeTargetAnalyzer analyzer;
  fs::path dir;
  ASSERT_TRUE(AnalyzeDb(R"json([
{
  "directory": "%TEMP_DIR%",
  "command": "cl.exe /nologo /TP -IProj/include /DWIN32 /D_WINDOWS /EHsc -std:c++17 -MDd /FoSub/CMakeFiles/app.dir/main.cpp.obj /c Proj/Src/main.cpp",
  "file": "Proj/Src/main.cpp",
  "output": "Sub/CMakeFiles/app.dir/main.cpp.obj"
},
{
  "directory": "%TEMP_DIR%",
  "command": "cl.exe /nologo /TP -IProj/include -IOtherLib/inc /DAPP_EXPORTS /EHsc /FoSub/CMakeFiles/app.dir/util.cpp.obj /c Proj/Src/util.cpp",
  "file": "Proj/Src/util.cpp",
  "output": "Sub/CMakeFiles/app.dir/util.cpp.obj"
}
])json", analyzer, &dir)) << analyzer.LastError();

  ASSERT_EQ(analyzer.Targets().size(), 1u);
  const auto& target = analyzer.Targets().front();
  EXPECT_EQ(target.name, "app");

  ASSERT_EQ(target.cppFiles.size(), 2u);
  EXPECT_EQ(target.cppFiles[0], (dir / "Proj" / "Src" / "main.cpp").generic_string());
  EXPECT_EQ(target.cppFiles[1], (dir / "Proj" / "Src" / "util.cpp").generic_string());

  // Include paths are absolute, normalized and de-duplicated, in the order
  // they appear across the target's entries.
  ASSERT_EQ(target.includePaths.size(), 2u);
  EXPECT_EQ(target.includePaths[0], (dir / "Proj" / "include").generic_string());
  EXPECT_EQ(target.includePaths[1], (dir / "OtherLib" / "inc").generic_string());

  ASSERT_EQ(target.defines.size(), 3u);
  EXPECT_EQ(target.defines[0], "-DWIN32");
  EXPECT_EQ(target.defines[1], "-D_WINDOWS");
  EXPECT_EQ(target.defines[2], "-DAPP_EXPORTS");

  EXPECT_EQ(target.cxxStandard, "-std=c++17");

  const Aternyx::CMake::CMakeTarget* found = analyzer.FindTarget("app");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->name, "app");
  EXPECT_EQ(analyzer.FindTarget("no_such_target"), nullptr);
}

// GNU/Clang-style flags with -isystem, separate -D arguments and the -o
// fallback for target attribution when the entry has no "output" field.
TEST(CMakeTargetAnalyzerTest, GnuStyleFlagsAndOutputFallback) {
  Aternyx::CMake::CMakeTargetAnalyzer analyzer;
  fs::path dir;
  ASSERT_TRUE(AnalyzeDb(R"json([
{
  "directory": "%TEMP_DIR%",
  "command": "clang++ -IProj/include -isystem 3rdparty/yaml -DNDEBUG -D FEATURE=1 -o Proj/CMakeFiles/gapp.dir/util.cpp.o -c Proj/Src/util.cpp -std=c++20",
  "file": "Proj/Src/util.cpp"
}
])json", analyzer, &dir)) << analyzer.LastError();

  ASSERT_EQ(analyzer.Targets().size(), 1u);
  const auto& target = analyzer.Targets().front();
  EXPECT_EQ(target.name, "gapp");
  ASSERT_EQ(target.cppFiles.size(), 1u);
  EXPECT_EQ(target.cppFiles[0], (dir / "Proj" / "Src" / "util.cpp").generic_string());

  ASSERT_EQ(target.includePaths.size(), 2u);
  EXPECT_EQ(target.includePaths[0], (dir / "Proj" / "include").generic_string());
  EXPECT_EQ(target.includePaths[1], (dir / "3rdparty" / "yaml").generic_string());

  ASSERT_EQ(target.defines.size(), 2u);
  EXPECT_EQ(target.defines[0], "-DNDEBUG");
  EXPECT_EQ(target.defines[1], "-DFEATURE=1");

  EXPECT_EQ(target.cxxStandard, "-std=c++20");
}

// ".." components inside include paths must collapse to normalized paths.
TEST(CMakeTargetAnalyzerTest, NormalizesDotDotComponents) {
  Aternyx::CMake::CMakeTargetAnalyzer analyzer;
  fs::path dir;
  ASSERT_TRUE(AnalyzeDb(R"json([
{
  "directory": "%TEMP_DIR%",
  "command": "cl.exe -IProj/Asset/.. -IProj/../Shared /FoSub/CMakeFiles/n.dir/a.cpp.obj /c Proj/Src/a.cpp",
  "file": "Proj/Src/a.cpp",
  "output": "Sub/CMakeFiles/n.dir/a.cpp.obj"
}
])json", analyzer, &dir)) << analyzer.LastError();

  ASSERT_EQ(analyzer.Targets().size(), 1u);
  const auto& target = analyzer.Targets().front();
  ASSERT_EQ(target.includePaths.size(), 2u);
  EXPECT_EQ(target.includePaths[0], (dir / "Proj").generic_string());
  EXPECT_EQ(target.includePaths[1], (dir / "Shared").generic_string());
}

// Entries whose target cannot be determined are skipped; non-C++ sources do
// not contribute files.
TEST(CMakeTargetAnalyzerTest, SkipsUnattributableEntriesAndNonCppSources) {
  Aternyx::CMake::CMakeTargetAnalyzer analyzer;
  fs::path dir;
  ASSERT_TRUE(AnalyzeDb(R"json([
{
  "directory": "%TEMP_DIR%",
  "command": "nasm.exe -f win64 boot.asm -o boot.obj"
},
{
  "directory": "%TEMP_DIR%",
  "command": "cl.exe /FoSub/CMakeFiles/legacy.dir/old.c.obj /c Proj/Src/old.c",
  "file": "Proj/Src/old.c",
  "output": "Sub/CMakeFiles/legacy.dir/old.c.obj"
}
])json", analyzer, &dir)) << analyzer.LastError();

  EXPECT_TRUE(analyzer.Targets().empty());
}

// "c++latest" (MSVC/clang-cl spelling) must be mapped to a standard value
// the GNU driver of libclang accepts.
TEST(CMakeTargetAnalyzerTest, MapsMsvcLatestStandard) {
  Aternyx::CMake::CMakeTargetAnalyzer analyzer;
  fs::path dir;
  ASSERT_TRUE(AnalyzeDb(R"json([
{
  "directory": "%TEMP_DIR%",
  "command": "cl.exe -IProj/include /FoSub/CMakeFiles/l.dir/a.cpp.obj /c Proj/Src/a.cpp -std:c++latest",
  "file": "Proj/Src/a.cpp",
  "output": "Sub/CMakeFiles/l.dir/a.cpp.obj"
}
])json", analyzer, &dir)) << analyzer.LastError();

  ASSERT_EQ(analyzer.Targets().size(), 1u);
  EXPECT_EQ(analyzer.Targets().front().cxxStandard, "-std=c++26");
}

TEST(CMakeTargetAnalyzerTest, MissingDatabaseFails) {
  Aternyx::CMake::CMakeTargetAnalyzer analyzer;
  EXPECT_FALSE(analyzer.Analyze((fs::temp_directory_path() / "no_such_dir_98765").string()));
  EXPECT_FALSE(analyzer.LastError().empty());
}
