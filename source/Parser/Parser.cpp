#include "Parser/Parser.h"

#include <clang-c/Index.h>
#include <iostream>

namespace Aternyx {

struct MetaParser::Impl {
  std::vector<std::string> namespaceStack;
  std::string sourcePath;
  AstTree astTree;
  // NOTE: no "-MG" here. It would turn missing includes into non-fatal
  // warnings and let clang error-recovery silently degrade field types to
  // int; missing includes must be hard errors (see BuildCursor).
  std::vector<std::string> arguments = {
      "-x",
      "c++",
      "-D__REFLECTION_PARSER__",
      "-DNDEBUG",
      "-D__clang__",
      "-w",
      "-M",
      "-ferror-limit=0",
      "-o clangLog.txt",
  };
  std::vector<std::string> extraArguments;

  void VisitChildren(const Cursor& cursor);
  void VisitNamespace(const Cursor& cursor);
  void VisitTypeDecl(const Cursor& cursor, int kind);
  std::string CurrentNamespace() const;
  void DebugAst();
};

MetaParser::MetaParser(const std::string& mainSourceFile,
                       const std::vector<std::string>& includePath,
                       const std::vector<std::string>& extraClangArgs)
    : impl_(std::make_unique<Impl>()) {
  impl_->sourcePath = mainSourceFile;
  for (const auto& path : includePath) {
    std::string argument = "-I" + path;
    impl_->arguments.push_back(argument);
  }
  // The default -std flag is dropped when the caller supplies one (e.g. from
  // a compile_commands.json entry) so the project's own standard wins.
  bool hasStdArg = false;
  for (const auto& extra : extraClangArgs) {
    if (extra.rfind("-std=", 0) == 0 || extra.rfind("/std:", 0) == 0)
      hasStdArg = true;
  }
  if (!hasStdArg)
    impl_->arguments.insert(impl_->arguments.begin() + 2, "-std=c++17");
  impl_->extraArguments = extraClangArgs;
}

MetaParser::~MetaParser() = default;

void MetaParser::BuildCursor() {
  CXIndex index = clang_createIndex(true, false);
  std::vector<const char*> arguments;
  for (const auto& argument : impl_->arguments) {
    arguments.emplace_back(argument.c_str());
  }
  for (const auto& argument : impl_->extraArguments) {
    arguments.emplace_back(argument.c_str());
  }
  auto translationUnit = clang_createTranslationUnitFromSourceFile(
      index, impl_->sourcePath.c_str(), static_cast<int>(arguments.size()), arguments.data(), 0, nullptr);
  if (translationUnit == nullptr) {
    if (index)
      clang_disposeIndex(index);
    throw MetaParseError("Failed to create translation unit for: " + impl_->sourcePath);
  }

  // Surface clang diagnostics: error-severity problems must not be swallowed
  // (clang error-recovery would otherwise degrade field types to int and
  // produce plausible-looking but broken generated code).
  std::vector<std::string> errorMessages;
  const unsigned diagnosticCount = clang_getNumDiagnostics(translationUnit);
  for (unsigned i = 0; i < diagnosticCount; ++i) {
    CXDiagnostic diagnostic = clang_getDiagnostic(translationUnit, i);
    const CXDiagnosticSeverity severity = clang_getDiagnosticSeverity(diagnostic);
    if (severity >= CXDiagnostic_Error) {
      CXString text = clang_formatDiagnostic(diagnostic, clang_defaultDiagnosticDisplayOptions());
      errorMessages.emplace_back(clang_getCString(text));
      clang_disposeString(text);
    } else if (severity == CXDiagnostic_Warning) {
      CXString text = clang_formatDiagnostic(diagnostic, clang_defaultDiagnosticDisplayOptions());
      std::cerr << "[MetaParser] warning: " << clang_getCString(text) << std::endl;
      clang_disposeString(text);
    }
    clang_disposeDiagnostic(diagnostic);
  }
  if (!errorMessages.empty()) {
    clang_disposeTranslationUnit(translationUnit);
    if (index)
      clang_disposeIndex(index);
    std::string message = "clang reported " + std::to_string(errorMessages.size()) +
                          " error(s) while parsing " + impl_->sourcePath + ":";
    for (const auto& error : errorMessages) {
      message += "\n  " + error;
    }
    throw MetaParseError(message);
  }

  auto cursor = clang_getTranslationUnitCursor(translationUnit);
  impl_->namespaceStack.clear();
  impl_->VisitChildren(cursor);
  clang_disposeTranslationUnit(translationUnit);
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
