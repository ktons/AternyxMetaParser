
#pragma once
#include <memory>

#include "Cursor/Cursor.h"
#include "Parser/MetaInfo.h"

namespace Aternyx {
class MetaParser {
 public:
  MetaParser(const std::string& mainSourceFile, const std::vector<std::string>& includePath);
  ~MetaParser();
  MetaParser(const MetaParser&) = delete;
  MetaParser& operator=(const MetaParser&) = delete;
  void BuildCursor();
  AstTree& GetAstTree();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace Aternyx