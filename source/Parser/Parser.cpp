#include "Parser/Parser.h"

#include <clang-c/Index.h>
#include <iostream>

namespace Aternyx {

struct MetaParser::Impl {
  int buildMode_{0};
  std::string currentNamespace_{""};
  MetaStruct tempStruct_;
  std::string sourcePath_;
  AstTree astTree_;
  std::vector<std::string> arguments_ = {
      "-x",
      "c++",
      "-std=c++11",
      "-D__REFLECTION_PARSER__",
      "-DNDEBUG",
      "-D__clang__",
      "-w",
      "-MG",
      "-M",
      "-ferror-limit=0",
      "-o clangLog.txt",
  };

  void VisitCursor(MetaParser* self, const Cursor& cursor, int deep);
  void BuildAst(MetaParser* self, const Cursor& cursor);
  void DebugAst();
  void EnqueueMetaStruct();
};

MetaParser::MetaParser(const std::string& mainSourceFile, const std::vector<std::string>& includePath)
    : impl_(std::make_unique<Impl>()) {
  impl_->sourcePath_ = mainSourceFile;
  impl_->arguments_.push_back(mainSourceFile);

  for (const auto& path : includePath) {
    impl_->arguments_.push_back("-I" + path);
  }
}

MetaParser::~MetaParser() = default;

AstTree& MetaParser::GetAstTree() {
  return impl_->astTree_;
}

void MetaParser::BuildCursor() {
  auto index = clang_createIndex(false, false);
  impl_->arguments_.push_back("-w");

  unsigned argc = static_cast<unsigned>(impl_->arguments_.size());
  const char** argv = new const char*[argc];
  for (unsigned i = 0; i < argc; ++i) {
    argv[i] = impl_->arguments_[i].c_str();
  }

  CXTranslationUnit tu = clang_parseTranslationUnit(index,
                                                    0,
                                                    argv,
                                                    argc,
                                                    nullptr,
                                                    0,
                                                    CXTranslationUnit_None);

  if (tu == nullptr) {
    std::cerr << "clang_parseTranslationUnit failed" << std::endl;
    delete[] argv;
    return;
  }

  Cursor rootCursor = clang_getTranslationUnitCursor(tu);

  impl_->BuildAst(this, rootCursor);

  clang_disposeTranslationUnit(tu);
  clang_disposeIndex(index);
  delete[] argv;
}

void MetaParser::Impl::BuildAst(MetaParser* self, const Cursor& cursor) {
  for (auto child : cursor.GetChildren()) {
    VisitCursor(self, child, 0);
  }
}

void MetaParser::Impl::EnqueueMetaStruct() {
  if (tempStruct_.fields.empty())
    return;

  astTree_.EmplaceBack(std::move(tempStruct_));
  astTree_.RegisterTypeName(tempStruct_.typeName);
  tempStruct_ = MetaStruct{};
}

void MetaParser::Impl::VisitCursor(MetaParser* self, const Cursor& cursor, int deep) {
  if (deep > 20)
    return;

  if (cursor.GetKind() == CXCursor_UnexposedAttr)
    return;
  if (cursor.GetKind() == CXCursor_InclusionDirective)
    return;

  if (deep > 5)
    return;

  auto spelling = cursor.GetSpelling();
  auto kind = cursor.GetKind();
  auto type = cursor.GetType();

  switch (kind) {
    case CXCursor_Namespace: {
      currentNamespace_ = spelling;
      break;
    }
    case CXCursor_StructDecl:
    case CXCursor_ClassDecl: {
      EnqueueMetaStruct();
      buildMode_ = 1;
      tempStruct_.kind = kind;
      tempStruct_.namespaceName = currentNamespace_;
      tempStruct_.simpleTypeName = spelling;

      if (currentNamespace_.empty())
        tempStruct_.typeName = spelling;
      else
        tempStruct_.typeName = currentNamespace_ + "::" + spelling;

      tempStruct_.sourceFilePath = cursor.GetSourceFile();

      // Parse children (base types, fields, etc.)
      for (auto child : cursor.GetChildren()) {
        auto childKind = child.GetKind();

        if (childKind == CXCursor_AnnotateAttr) {
          tempStruct_.AddAttributes(child.GetSpelling());
        }
        if (childKind == CXCursor_CXXBaseSpecifier) {
          tempStruct_.baseTypeName = child.GetType().GetDisplayName();

          for (auto subChild : child.GetChildren()) {
            VisitCursor(self, subChild, deep + 1);
          }
        }
        if (childKind == CXCursor_FieldDecl) {
          MetaField field;
          field.name = child.GetSpelling();
          field.type = child.GetType().GetDisplayName();
          field.metaFieldType = MetaFieldTypeInfo::Property;

          tempStruct_.fields.emplace_back(field);

          // Check children of field for attributes
          for (auto subChild : child.GetChildren()) {
            if (subChild.GetKind() == CXCursor_AnnotateAttr) {
              tempStruct_.fields.back().AddAttributes(subChild.GetSpelling());
            }
          }
        }
      }

      buildMode_ = 0;
      EnqueueMetaStruct();
      break;
    }
    default:
      break;
  }

  for (auto child : cursor.GetChildren()) {
    VisitCursor(self, child, deep + 1);
  }
}

void MetaParser::Impl::DebugAst() {
  for (const auto& metaStruct : astTree_.metaStructList) {
    std::cout << "name: " << metaStruct.typeName << std::endl;
  }
}

}  // namespace Aternyx
