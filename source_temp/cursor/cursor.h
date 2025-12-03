#pragma once

#include <vector>

#include "cursor_type.h"

inline void clangToString(const CXString& str, std::string& out) {
  auto cstr = clang_getCString(str);

  out = cstr;

  clang_disposeString(str);
}

class Cursor {
 public:
  typedef std::vector<Cursor> List;

  typedef CXCursorVisitor Visitor;

  Cursor(const CXCursor& handle);

  CXCursorKind getKind(void) const;

  std::string getSpelling(void) const;
  std::string getDisplayName(void) const;

  std::string getSourceFile(void) const;

  bool isDefinition(void) const;

  CursorType getType(void) const;

  List getChildren(void) const;
  void visitChildren(Visitor visitor, void* data = nullptr);

 private:
  CXCursor m_handle;
};