#pragma once

#include <vector>

#include "Cursor/CursorType.h"

inline void ClangToString(const CXString& str, std::string& out) {
  auto cstr = clang_getCString(str);
  out = cstr;
  clang_disposeString(str);
}

class Cursor {
 public:
  typedef std::vector<Cursor> List;
  typedef CXCursorVisitor Visitor;

  Cursor(const CXCursor& handle);

  CXCursorKind GetKind(void) const;
  std::string GetSpelling(void) const;
  std::string GetDisplayName(void) const;
  std::string GetSourceFile(void) const;
  bool IsDefinition(void) const;
  CursorType GetType(void) const;
  CursorType GetResultType(void) const;
  List GetChildren(void) const;
  void Visit(Visitor visitor, void* data = nullptr) const;

 private:
  CXCursor handle_;
};
