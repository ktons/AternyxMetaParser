#pragma once

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
  std::string GetTypeName(const std::string& typeName);

 private:
  std::unordered_map<std::string, uint32_t> metaStructMap_;
  std::unordered_set<std::string> typeNameSet_;

  void InnerGetTypeName(const std::string& typeName, std::string& finalTypeName);
  std::string GetFullTypeName(const std::string& typeName);
};
}  // namespace Aternyx