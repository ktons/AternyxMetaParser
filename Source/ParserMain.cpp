// reference: https://github.com/AustinBrunkhorst/CPP-Reflection

#include "CodeGenerator/CodeGenerator.h"
#include "Parser/Parser.h"

namespace mustache = kainjow::mustache;

// note: argv 0 is this exe
int main(int argc, char* argv[]) {
  if (argc < 3 || argv[1] == nullptr || argv[2] == nullptr) {
    return -1;
  }
  char* main_source_file = argv[1];

  std::vector<std::string> included_paths;
  for (int i = 2; i < argc; i++) {
    included_paths.emplace_back(argv[i]);
  }
  Aternyx::MetaParser parser{main_source_file, included_paths};
  parser.BuildCursor();
  // parser.GetAstTree().DebugInfo();
  Aternyx::CodeGenerator generator;
  generator.Init();
  generator.SetAstTree(&parser.GetAstTree());
  generator.Run();

  return 0;
}