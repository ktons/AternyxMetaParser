#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "CMakeAnalyzer/CompileCommands.h"

namespace fs = std::filesystem;

namespace {

int g_tempCounter = 0;

fs::path CreateTempDir() {
  fs::path dir = fs::temp_directory_path() / ("compile_commands_test_" + std::to_string(++g_tempCounter));
  fs::create_directories(dir);
  return dir;
}

void WriteFile(const fs::path& path, const std::string& content) {
  std::ofstream ofs(path, std::ios::binary);
  ofs << content;
  ofs.close();
}

// Writes a compile db whose `%TEMP_DIR%` tokens are replaced by the actual
// (portable, forward-slash) directory path.
void WriteDb(const fs::path& dir, const std::string& jsonText) {
  std::string content = jsonText;
  const std::string token = "%TEMP_DIR%";
  const std::string replacement = dir.generic_string();
  size_t position = content.find(token);
  while (position != std::string::npos) {
    content.replace(position, token.size(), replacement);
    position = content.find(token, position + replacement.size());
  }
  WriteFile(dir / "compile_commands.json", content);
}

}  // namespace

TEST(SplitCommandLineTest, PlainArguments) {
  auto args = Aternyx::CMake::SplitCommandLine("cl.exe /nologo /TP -Iinclude main.cpp");
  ASSERT_EQ(args.size(), 5u);
  EXPECT_EQ(args[0], "cl.exe");
  EXPECT_EQ(args[1], "/nologo");
  EXPECT_EQ(args[2], "/TP");
  EXPECT_EQ(args[3], "-Iinclude");
  EXPECT_EQ(args[4], "main.cpp");
}

TEST(SplitCommandLineTest, QuotedPathWithSpaces) {
  // MSVC-style db command: quoted compiler path containing spaces.
  auto args = Aternyx::CMake::SplitCommandLine(
      "\"F:\\DevKit\\Visual Studio 2026\\VC\\bin\\cl.exe\"  /nologo main.cpp");
  ASSERT_EQ(args.size(), 3u);
  EXPECT_EQ(args[0], "F:\\DevKit\\Visual Studio 2026\\VC\\bin\\cl.exe");
  EXPECT_EQ(args[1], "/nologo");
  EXPECT_EQ(args[2], "main.cpp");
}

TEST(SplitCommandLineTest, BackslashQuoteEscapes) {
  // 2 backslashes + quote -> 1 backslash, quote toggles; the token continues
  // until the next unquoted whitespace.
  auto even = Aternyx::CMake::SplitCommandLine("a\\\\\"b\" c");
  ASSERT_EQ(even.size(), 2u);
  EXPECT_EQ(even[0], "a\\b");
  EXPECT_EQ(even[1], "c");

  // An unclosed quote keeps consuming whitespace: one single argument.
  auto unclosed = Aternyx::CMake::SplitCommandLine("a\\\\\"b c");
  ASSERT_EQ(unclosed.size(), 1u);
  EXPECT_EQ(unclosed[0], "a\\b c");

  // 3 backslashes + quote -> 1 backslash + literal quote, quoting continues.
  auto odd = Aternyx::CMake::SplitCommandLine("a\\\\\\\"b c");
  ASSERT_EQ(odd.size(), 2u);
  EXPECT_EQ(odd[0], "a\\\"b");
  EXPECT_EQ(odd[1], "c");
}

TEST(SplitCommandLineTest, EmptyArgument) {
  auto args = Aternyx::CMake::SplitCommandLine("\"\" x");
  ASSERT_EQ(args.size(), 2u);
  EXPECT_EQ(args[0], "");
  EXPECT_EQ(args[1], "x");
}

TEST(CompileCommandDbTest, LoadFromDirectoryAndResolveRelativePaths) {
  fs::path dir = CreateTempDir();
  WriteDb(dir, R"json([
{
  "directory": "%TEMP_DIR%",
  "command": "cl.exe /nologo /FoSub/CMakeFiles/app.dir/main.cpp.obj /c Proj/Src/main.cpp",
  "file": "Proj/Src/main.cpp",
  "output": "Sub/CMakeFiles/app.dir/main.cpp.obj"
}
])json");

  Aternyx::CMake::CompileCommandDb db;
  ASSERT_TRUE(db.Load(dir.string())) << db.LastError();
  ASSERT_EQ(db.Entries().size(), 1u);

  const auto& entry = db.Entries().front();
  EXPECT_EQ(entry.directory, dir.generic_string());
  EXPECT_EQ(entry.file, (dir / "Proj" / "Src" / "main.cpp").generic_string());
  EXPECT_EQ(entry.output, (dir / "Sub" / "CMakeFiles" / "app.dir" / "main.cpp.obj").generic_string());
  ASSERT_EQ(entry.arguments.size(), 5u);
  EXPECT_EQ(entry.arguments.front(), "cl.exe");
}

TEST(CompileCommandDbTest, PrefersArgumentsArrayOverCommand) {
  fs::path dir = CreateTempDir();
  WriteDb(dir, R"json([
{
  "directory": "%TEMP_DIR%",
  "arguments": ["clang++", "-c", "a.cpp"],
  "command": "ignored because arguments exists",
  "file": "a.cpp"
}
])json");

  Aternyx::CMake::CompileCommandDb db;
  ASSERT_TRUE(db.Load(dir.string())) << db.LastError();
  ASSERT_EQ(db.Entries().size(), 1u);
  ASSERT_EQ(db.Entries().front().arguments.size(), 3u);
  EXPECT_EQ(db.Entries().front().arguments[0], "clang++");
}

TEST(CompileCommandDbTest, ExpandsResponseFiles) {
  fs::path dir = CreateTempDir();
  WriteFile(dir / "flags.rsp", "-Iproj/include\n-DNDEBUG");
  const std::string rspToken = "@FLAGS_RSP";
  WriteDb(dir, R"json([
{
  "directory": "%TEMP_DIR%",
  "command": "cl.exe \")json" + rspToken + R"json(\" main.cpp",
  "file": "main.cpp"
}
])json");
  // Substitute the response-file path (forward slashes, JSON-safe).
  {
    std::ifstream ifs(dir / "compile_commands.json", std::ios::binary);
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string content = buffer.str();
    const std::string replacement = "@" + (dir / "flags.rsp").generic_string();
    content.replace(content.find(rspToken), rspToken.size(), replacement);
    WriteFile(dir / "compile_commands.json", content);
  }

  Aternyx::CMake::CompileCommandDb db;
  ASSERT_TRUE(db.Load(dir.string())) << db.LastError();
  ASSERT_EQ(db.Entries().size(), 1u);
  const auto& args = db.Entries().front().arguments;
  ASSERT_EQ(args.size(), 4u);
  EXPECT_EQ(args[1], "-Iproj/include");
  EXPECT_EQ(args[2], "-DNDEBUG");
  EXPECT_EQ(args[3], "main.cpp");
}

TEST(CompileCommandDbTest, MissingDatabaseReportsError) {
  Aternyx::CMake::CompileCommandDb db;
  EXPECT_FALSE(db.Load((fs::temp_directory_path() / "no_such_db_dir_12345").string()));
  EXPECT_NE(db.LastError().find("not found"), std::string::npos);
}

TEST(CompileCommandDbTest, MalformedJsonReportsError) {
  fs::path dir = CreateTempDir();
  WriteFile(dir / "compile_commands.json", "{ not valid json ");
  Aternyx::CMake::CompileCommandDb db;
  EXPECT_FALSE(db.Load(dir.string()));
  EXPECT_FALSE(db.LastError().empty());
}
