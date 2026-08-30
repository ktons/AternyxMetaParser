#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Aternyx {

enum class MetaFieldTypeInfo {
  Property,
  Function,
};

struct MetaField {
  std::string name;
  std::string type;
  MetaFieldTypeInfo metaFieldType{MetaFieldTypeInfo::Property};
  std::vector<std::string> attributes;
  void AddAttributes(const std::string& attributeStr);
};

struct MetaStruct {
  int kind;
  std::vector<MetaField> fields;
  std::string sourceFilePath;
  std::string namespaceName;
  std::string simpleTypeName;
  std::string typeName;
  std::string baseTypeName;
  std::vector<std::string> attributes;
  std::vector<uint32_t> derivedTypeIndex;
  void AddAttributes(const std::string& attributeStr);
};

struct AstTree {
  void DebugInfo() const;
  std::vector<MetaStruct> metaStructList;
  std::string currentNamespace;

  MetaStruct& GetMetaStruct(uint32_t index) {
    return metaStructList.at(index);
  }

  void EmplaceBack(MetaStruct&& metaStruct);
  void RegisterTypeName(const std::string& fullTypeName);
  // Rewrites a type spelling so every project-defined type it mentions is
  // fully qualified: identifiers are resolved against the registered type
  // names (see ResolveRegisteredTypeName), everything else (std::, builtins,
  // decorations like `const` / `*` / template punctuation) passes through.
  std::string GetTypeName(const std::string& typeName) const;
  // Merges `other` into this tree. Types already present (matched by fully
  // qualified name) are skipped, so annotated headers included by many
  // translation units are collected only once. Cross-references
  // (MetaStruct::derivedTypeIndex) are remapped to this tree, including
  // base-class links that only become visible after the merge.
  void MergeFrom(AstTree&& other);

 private:
  std::unordered_map<std::string, uint32_t> metaStructMap_;
  std::unordered_set<std::string> typeNameSet_;
  // Memoized GetTypeName results, keyed by input spelling + the namespace it
  // was resolved in (both feed the resolution).
  mutable std::unordered_map<std::string, std::string> resolutionCache_;

  // Resolves one (possibly namespace-qualified) identifier spelling to its
  // registered fully qualified name. Resolution order:
  //   1. the spelling as written (already qualified, or a std::/builtin name),
  //   2. the enclosing namespace chain of the declaring type (innermost
  //      first), preserving normal C++ name hiding,
  //   3. registered names ending in the written spelling (partial
  //      qualification, e.g. `Resource::AssetMetaInfo`),
  //   4. the unique registered type whose simple name matches; when several
  //      match, the one sharing the longest namespace prefix with the
  //      declaring type wins (a warning goes to stderr) — deterministic and
  //      compilable, and the ambiguity is surfaced.
  // Returns the spelling unchanged when nothing matches.
  std::string ResolveRegisteredTypeName(const std::string& name) const;
};

}  // namespace Aternyx
