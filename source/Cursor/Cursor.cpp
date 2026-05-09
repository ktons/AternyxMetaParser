#include "Cursor/Cursor.h"

Cursor::Cursor(const CXCursor& handle) : handle_(handle) {}

CXCursorKind Cursor::GetKind(void) const {
  return handle_.kind;
}

std::string Cursor::GetSpelling(void) const {
  std::string spelling;
  ClangToString(clang_getCursorSpelling(handle_), spelling);
  return spelling;
}

std::string Cursor::GetDisplayName(void) const {
  std::string displayName;
  ClangToString(clang_getCursorDisplayName(handle_), displayName);
  return displayName;
}

std::string Cursor::GetSourceFile(void) const {
  auto range = clang_Cursor_getSpellingNameRange(handle_, 0, 0);
  auto start = clang_getRangeStart(range);

  CXFile file;
  unsigned line, column, offset;
  clang_getFileLocation(start, &file, &line, &column, &offset);

  std::string filename;
  ClangToString(clang_getFileName(file), filename);
  return filename;
}

bool Cursor::IsDefinition(void) const {
  return clang_isCursorDefinition(handle_) != 0;
}

CursorType Cursor::GetType(void) const {
  return clang_getCursorType(handle_);
}

CursorType Cursor::GetResultType(void) const {
  return clang_getCursorResultType(handle_);
}

Cursor::List Cursor::GetChildren(void) const {
  List children;

  auto visitor = [](CXCursor cursor, CXCursor parent, CXClientData data) {
    auto container = static_cast<List*>(data);
    container->emplace_back(cursor);

    if (cursor.kind == CXCursor_LastPreprocessing)
      return CXChildVisit_Break;

    return CXChildVisit_Continue;
  };

  clang_visitChildren(handle_, visitor, &children);

  return children;
}

void Cursor::Visit(Visitor visitor, void* data) const {
  clang_visitChildren(handle_, visitor, data);
}
