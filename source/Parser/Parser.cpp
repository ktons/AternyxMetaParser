#include "Parser/Parser.h"

#include <clang-c/Index.h>
#include <iostream>

namespace Aternyx {

struct MetaParser::Impl {
  std::vector<std::string> namespaceStack;
  std::string sourcePath;
  AstTree astTree;
  std::vector<std::string> arguments = {
      "-x",
      "c++",
      "-std=c++17",
      "-D__REFLECTION_PARSER__",
      "-DNDEBUG",
      "-D__clang__",
      "-w",
      "-MG",
      "-M",
      "-ferror-limit=0",
      "-o clangLog.txt",
  };

  void VisitChildren(const Cursor& cursor);
  void VisitNamespace(const Cursor& cursor);
  void VisitTypeDecl(const Cursor& cursor, int kind);
  std::string CurrentNamespace() const;
  void DebugAst();
};

MetaParser::MetaParser(const std::string& mainSourceFile, const std::vector<std::string>& includePath)
    : impl_(std::make_unique<Impl>()) {
  impl_->sourcePath = mainSourceFile;
  for (const auto& path : includePath) {
    std::string argument = "-I" + path;
    impl_->arguments.push_back(argument);
  }
}

MetaParser::~MetaParser() = default;

void MetaParser::BuildCursor() {
  CXIndex index = clang_createIndex(true, false);
  std::vector<const char*> arguments;
  for (const auto& argument : impl_->arguments) {
    arguments.emplace_back(argument.c_str());
  }
  auto translationUnit = clang_createTranslationUnitFromSourceFile(
      index, impl_->sourcePath.c_str(), static_cast<int>(arguments.size()), arguments.data(), 0, nullptr);
  auto cursor = clang_getTranslationUnitCursor(translationUnit);
  impl_->namespaceStack.clear();
  impl_->VisitChildren(cursor);
  if (index) {
    clang_disposeIndex(index);
  }
  // impl_->DebugAst();
}

AstTree& MetaParser::GetAstTree() {
  return impl_->astTree;
}

void MetaParser::Impl::VisitChildren(const Cursor& cursor) {
  for (const auto& child : cursor.GetChildren()) {
    const int kind = static_cast<int>(child.GetKind());
    switch (kind) {
      case CXCursor_Namespace:
        VisitNamespace(child);
        break;
      case CXCursor_StructDecl:
      case CXCursor_ClassDecl:
      case CXCursor_EnumDecl:
        VisitTypeDecl(child, kind);
        break;
      default:
        break;
    }
  }
}

void MetaParser::Impl::VisitNamespace(const Cursor& cursor) {
  namespaceStack.push_back(cursor.GetDisplayName());
  astTree.currentNamespace = CurrentNamespace();
  VisitChildren(cursor);
  namespaceStack.pop_back();
  astTree.currentNamespace = CurrentNamespace();
}

std::string MetaParser::Impl::CurrentNamespace() const {
  std::string joined;
  for (size_t i = 0; i < namespaceStack.size(); ++i) {
    if (i > 0) {
      joined += "::";
    }
    joined += namespaceStack[i];
  }
  return joined;
}

void MetaParser::Impl::VisitTypeDecl(const Cursor& cursor, int kind) {
  const std::string typeName = cursor.GetType().GetDisplayName();
  astTree.RegisterTypeName(typeName);
  if (!cursor.IsDefinition()) {
    return;
  }

  MetaStruct metaStruct;
  metaStruct.kind = kind;
  metaStruct.sourceFilePath = cursor.GetSourceFile();
  metaStruct.typeName = typeName;
  std::string simpleTypeName = typeName;
  auto typeSplitIndex = typeName.find_last_of("::");
  if (typeSplitIndex != std::string::npos) {
    metaStruct.namespaceName = typeName.substr(0, typeSplitIndex - 1);
    simpleTypeName = typeName.substr(typeSplitIndex + 1, typeName.size());
  }
  metaStruct.simpleTypeName = simpleTypeName;

  for (const auto& child : cursor.GetChildren()) {
    const int childKind = static_cast<int>(child.GetKind());
    switch (childKind) {
      case CXCursor_AnnotateAttr:
        metaStruct.AddAttributes(child.GetDisplayName());
        break;
      case CXCursor_CXXBaseSpecifier:
        metaStruct.baseTypeName = astTree.GetTypeName(child.GetType().GetDisplayName());
        break;
      case CXCursor_FieldDecl:
      case CXCursor_CXXMethod: {
        MetaField field{
            .name = child.GetDisplayName(),
            .type = astTree.GetTypeName(child.GetType().GetDisplayName()),
            .metaFieldType =
                childKind == CXCursor_CXXMethod ? MetaFieldTypeInfo::Function : MetaFieldTypeInfo::Property,
            .attributes = {},
        };
        for (const auto& attr : child.GetChildren()) {
          if (attr.GetKind() == CXCursor_AnnotateAttr) {
            field.AddAttributes(attr.GetDisplayName());
          }
        }
        metaStruct.fields.push_back(std::move(field));
        break;
      }
      default:
        break;
    }
  }

  if ((!metaStruct.attributes.empty() && !metaStruct.fields.empty()) || metaStruct.kind == CXCursor_EnumDecl) {
    astTree.EmplaceBack(std::move(metaStruct));
  }

  VisitChildren(cursor);
}

void MetaParser::Impl::DebugAst() {
  for (const auto& metaStruct : astTree.metaStructList) {
    std::string attributeStruct;
    for (const auto& attribute : metaStruct.attributes) {
      attributeStruct += attribute + ", ";
    }
    std::cout << attributeStruct << std::endl;
    for (const auto& metaField : metaStruct.fields) {
      std::string fieldAttributes;
      for (const auto& attribute : metaField.attributes) {
        fieldAttributes += attribute + ", ";
      }
      std::cout << fieldAttributes << std::endl;
    }
  }
}

}  // namespace Aternyx
