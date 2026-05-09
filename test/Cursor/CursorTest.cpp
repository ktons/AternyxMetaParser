#include <clang-c/Index.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "Cursor/Cursor.h"
#include "Cursor/CursorType.h"

namespace fs = std::filesystem;

// Cross-platform temp file helper
static fs::path CreateTempFile(const std::string& content) {
  static int counter = 0;
  fs::path path = fs::temp_directory_path() / ("test_trivial_" + std::to_string(++counter) + ".cpp");
  std::ofstream ofs(path);
  ofs << content;
  ofs.close();
  return path;
}

// Test Cursor construction from a null handle
TEST(CursorTest, NullHandle) {
  CXCursor nullCursor = clang_getNullCursor();
  Cursor cursor(nullCursor);
  EXPECT_EQ(cursor.GetKind(), CXCursor_FirstInvalid);
  EXPECT_TRUE(cursor.GetSpelling().empty());
}

// Test CursorType from a null type
TEST(CursorTypeTest, NullType) {
  CXType nullType = {};
  CursorType type(nullType);
  EXPECT_EQ(type.GetKind(), CXType_Invalid);
  EXPECT_TRUE(type.GetDisplayName().empty());
}

// Test that clang_createIndex succeeds (basic libclang sanity)
TEST(LibclangTest, CreateIndex) {
  CXIndex index = clang_createIndex(true, false);
  EXPECT_NE(index, nullptr);
  clang_disposeIndex(index);
}

// Test parsing a trivial source file via TU
TEST(LibclangTest, ParseTrivialSource) {
  fs::path tmpPath = CreateTempFile("int x = 42;");

  CXIndex index = clang_createIndex(true, false);

  const char* args[] = {"-x", "c++", "-std=c++11"};
  auto tu = clang_createTranslationUnitFromSourceFile(
      index, tmpPath.string().c_str(), 3, args, 0, nullptr);
  ASSERT_NE(tu, nullptr);

  CXCursor rootCursor = clang_getTranslationUnitCursor(tu);
  Cursor root(rootCursor);
  EXPECT_NE(root.GetKind(), CXCursor_FirstInvalid);

  auto children = root.GetChildren();
  EXPECT_FALSE(children.empty());

  clang_disposeTranslationUnit(tu);
  clang_disposeIndex(index);
  fs::remove(tmpPath);
}
