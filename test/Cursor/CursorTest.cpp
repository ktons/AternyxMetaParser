#include <clang-c/Index.h>
#include <gtest/gtest.h>

#include "Cursor/Cursor.h"
#include "Cursor/CursorType.h"

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
  char tmpPath[] = "/tmp/test_trivial_XXXXXX";
  int fd = mkstemp(tmpPath);
  ASSERT_NE(fd, -1);
  FILE* f = fdopen(fd, "w");
  ASSERT_NE(f, nullptr);
  fputs("int x = 42;", f);
  fclose(f);

  CXIndex index = clang_createIndex(true, false);

  const char* args[] = {"-x", "c++", "-std=c++11"};
  auto tu = clang_createTranslationUnitFromSourceFile(
      index, tmpPath, 3, args, 0, nullptr);
  ASSERT_NE(tu, nullptr);

  CXCursor rootCursor = clang_getTranslationUnitCursor(tu);
  Cursor root(rootCursor);
  EXPECT_NE(root.GetKind(), CXCursor_FirstInvalid);

  auto children = root.GetChildren();
  EXPECT_FALSE(children.empty());

  clang_disposeTranslationUnit(tu);
  clang_disposeIndex(index);
  unlink(tmpPath);
}
