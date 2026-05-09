// reference: https://github.com/AustinBrunkhorst/CPP-Reflection

#include <iostream>

#include "CodeGenerator/CodeGenerator.h"
#include "Config/ArgConfig.h"
#include "Parser/Parser.h"

// note: argv 0 is this exe
int main(int argc, char* argv[]) {
  if (!ArgConfig::Instance().ParseArgs(argc, argv))
    return -1;

  Aternyx::MetaParser parser{ArgConfig::Instance().sourceFile_, ArgConfig::Instance().includePaths_};
  parser.BuildCursor();

  Aternyx::CodeGenerator generator;
  generator.Init();
  generator.SetAstTree(&parser.GetAstTree());
  generator.Run();

  return 0;
}
