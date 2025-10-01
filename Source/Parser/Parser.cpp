#include "Parser/Parser.h"

#include <iostream>

namespace Aternyx {

struct MetaParser::Impl {
  int buildMode{0};
  std::string currentNamespace{"umi_engine"};
  MetaStruct tempStruct;
  std::string sourcePath;
  AstTree astTree;
  std::vector<std::string> arguments = {
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
  bool TryGetTypeDefine(
      MetaParser* self, const Cursor& cursor, const std::string& name, const std::string& typeName, int kind);
  bool TryGetTypeAttribute(const std::string& name, const std::string& typeName, int kind);
  bool TryGetBaseType(const std::string& name, const std::string& typeName, int kind);
  bool TryGetFiled(MetaParser* self, const std::string& name, const std::string& typeName, int kind);
  bool TryGetFieldAttribute(const std::string& name, const std::string& typeName, int kind);
  bool TryGetTypeDefineEnd(MetaParser* self, const std::string& name, const std::string& typeName, int kind);
};

MetaParser::MetaParser(const std::string& mainSourceFile, const std::vector<std::string>& includePath)
    : impl_(std::make_unique<Impl>()) {
  impl_->sourcePath = mainSourceFile;
  impl_->arguments.push_back("-includeparser/precompile/core.h");
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
  impl_->buildMode = 0;
  impl_->VisitCursor(this, cursor, 0);
  if (index) {
    clang_disposeIndex(index);
  }
  impl_->DebugAst();
}

AstTree& MetaParser::GetAstTree() {
  return impl_->astTree;
}

void MetaParser::Impl::VisitCursor(MetaParser* self, const Cursor& cursor, int deep) {
  for (const auto& child : cursor.getChildren()) {
    BuildAst(self, child);
    VisitCursor(self, child, deep + 1);
  }
}

void MetaParser::Impl::BuildAst(MetaParser* self, const Cursor& cursor) {
  int kind = static_cast<int>(cursor.getKind());
  const std::string& typeName = cursor.getType().GetDisplayName();
  const std::string& name = cursor.getDisplayName();
  if (kind == CXCursor_Namespace) {
    astTree.currentNamespace = name;
  }
  if (kind == CXCursor_TemplateRef) {
    astTree.RegisterTypeName(astTree.currentNamespace + "::" + name);
  }
  switch (buildMode) {
    case 1: {
      if (TryGetTypeAttribute(name, typeName, kind)) {
        buildMode = 2;
      } else {
        buildMode = 0;
      }
      break;
    }
    case 0: {
      if (TryGetTypeDefine(self, cursor, name, typeName, kind)) {
        buildMode = 1;
      }
      break;
    }
    case 2: {
      if (TryGetBaseType(name, typeName, kind) || TryGetFiled(self, name, typeName, kind) ||
          TryGetFieldAttribute(name, typeName, kind)) {
        buildMode = 2;
      } else if (TryGetTypeDefine(self, cursor, name, typeName, kind)) {
        buildMode = 1;
      } else if (TryGetTypeDefineEnd(self, name, typeName, kind)) {
        buildMode = 0;
      }
      break;
    }
  }
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

void MetaParser::Impl::EnqueueMetaStruct() {
  if ((!tempStruct.attributes.empty() && !tempStruct.fields.empty()) || tempStruct.kind == CXCursor_EnumDecl) {
    astTree.EmplaceBack(std::move(tempStruct));
  }
  tempStruct = {};
}

bool MetaParser::Impl::TryGetTypeDefine(
    MetaParser* self, const Cursor& cursor, const std::string& name, const std::string& typeName, int kind) {
  if (kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl || kind == CXCursor_EnumDecl) {
    astTree.RegisterTypeName(typeName);
    EnqueueMetaStruct();
    std::string namespaceName;
    std::string simpleTypeName = typeName;
    auto typeSplitIndex = typeName.find_last_of("::");
    if (typeSplitIndex != std::string::npos) {
      namespaceName = typeName.substr(0, typeSplitIndex - 1);
      simpleTypeName = typeName.substr(typeSplitIndex + 1, typeName.size());
    }
    tempStruct = {
        .kind = kind,
        .sourceFilePath = cursor.getSourceFile(),
        .namespaceName = namespaceName,
        .simpleTypeName = simpleTypeName,
        .typeName = typeName,
    };
    return true;
  }
  return false;
}

bool MetaParser::Impl::TryGetTypeAttribute(const std::string& name, const std::string& typeName, int kind) {
  if (kind == CXCursor_AnnotateAttr) {
    tempStruct.AddAttributes(name);
    return true;
  } else {
    tempStruct = {};
    return false;
  }
}

bool MetaParser::Impl::TryGetBaseType(const std::string& name, const std::string& typeName, int kind) {
  if (kind == CXCursor_CXXBaseSpecifier) {
    tempStruct.baseTypeName = astTree.GetTypeName(typeName);
    return true;
  }
  return false;
}

bool MetaParser::Impl::TryGetFiled(MetaParser* self, const std::string& name, const std::string& typeName, int kind) {
  if (kind == CXCursor_FieldDecl || kind == CXCursor_CXXMethod) {
    tempStruct.fields.emplace_back(MetaField{
        .name = name,
        .type = astTree.GetTypeName(typeName),
        .metaFieldType = kind == CXCursor_CXXMethod ? MetaFieldTypeInfo::Function : MetaFieldTypeInfo::Property,
        .attributes = {},
    });
    return true;
  }
  return false;
}

bool MetaParser::Impl::TryGetFieldAttribute(const std::string& name, const std::string& typeName, int kind) {
  if (kind == CXCursor_AnnotateAttr) {
    tempStruct.fields.back().AddAttributes(name);
    return true;
  }
  return false;
}

bool MetaParser::Impl::TryGetTypeDefineEnd(MetaParser* self,
                                           const std::string& name,
                                           const std::string& typeName,
                                           int kind) {
  if (kind == CXCursor_Namespace) {
    EnqueueMetaStruct();
    return true;
  }
  return false;
}

}  // namespace Aternyx