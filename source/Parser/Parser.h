#pragma once
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Cursor/Cursor.h"
#include "Parser/MetaInfo.h"

namespace Aternyx {

// Thrown by MetaParser::BuildCursor when libclang reports error-severity
// diagnostics for the translation unit (e.g. unresolved includes). Carries
// every formatted diagnostic message so the caller sees the full picture.
struct MetaParseError : std::runtime_error {
  explicit MetaParseError(const std::string& message) : std::runtime_error(message) {}
};

class MetaParser {
 public:
  MetaParser(const std::string& mainSourceFile,
             const std::vector<std::string>& includePath,
             const std::vector<std::string>& extraClangArgs = {});
  ~MetaParser();
  MetaParser(const MetaParser&) = delete;
  MetaParser& operator=(const MetaParser&) = delete;
  // Parses the translation unit and collects the annotated AST.
  // Throws MetaParseError when clang reports error-severity diagnostics.
  void BuildCursor();
  AstTree& GetAstTree();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace Aternyx
