// reference: https://github.com/AustinBrunkhorst/CPP-Reflection

#include <iostream>

#include "code_generator/code_generator.h"
#include "config/arg_config.h"
#include "parser/parser.h"

// note: argv 0 is this exe
int main(int argc, char* argv[]) {
  if (!ArgConfig::Instance().ParseArgs(argc, argv))
    return -1;
  // ArgConfig::Instance().DebugInfo();
  Aternyx::MetaParser parser{ArgConfig::Instance().source_file, ArgConfig::Instance().include_paths};
  parser.BuildCursor();
  // parser.GetAstTree().DebugInfo();
  Aternyx::CodeGenerator generator;
  generator.Init();
  generator.SetAstTree(&parser.GetAstTree());
  generator.Run();

  return 0;
}