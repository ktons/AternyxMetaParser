#include "Parser/MetaInfo.h"

#include <iostream>
#include <unordered_set>

#include "Utils/Utils.h"

namespace Aternyx {

void AstTree::DebugInfo() const {
  std::cout << "metaStructMap_:" << std::endl;
  for (const auto& [key, value] : metaStructMap_) {
    std::cout << "  [" << key << "] => " << value << std::endl;
  }
  std::cout << "typeNameSet_:" << std::endl;
  for (const auto& name : typeNameSet_) {
    std::cout << "  " << name << std::endl;
  }
}

void MetaField::AddAttributes(const std::string& attributeStr) {
  auto splitStr = Aternyx::StringLib::Split(attributeStr, ",");
  for (const auto& str : splitStr) {
    attributes.emplace_back(Aternyx::StringLib::Trim(str));
  }
}

void MetaStruct::AddAttributes(const std::string& attributeStr) {
  auto splitStr = Aternyx::StringLib::Split(attributeStr, ",");
  for (const auto& str : splitStr) {
    attributes.emplace_back(Aternyx::StringLib::Trim(str));
  }
}

// -------------------------------------------- ast tree --------------------------------------------
void AstTree::EmplaceBack(Aternyx::MetaStruct&& metaStruct) {
  size_t index = metaStructList.size();
  metaStructMap_[metaStruct.typeName] = index;
  metaStructList.emplace_back(metaStruct);
  if (!metaStruct.baseTypeName.empty()) {
    auto baseIt = metaStructMap_.find(metaStruct.baseTypeName);
    if (baseIt != metaStructMap_.end()) {
      metaStructList[baseIt->second].derivedTypeIndex.push_back(static_cast<uint32_t>(index));
    }
  }
}

void AstTree::RegisterTypeName(const std::string& fullTypeName) {
  typeNameSet_.insert(fullTypeName);
}

std::string AstTree::GetTypeName(const std::string& typeName) {
  std::string finalTypeName;
  InnerGetTypeName(typeName, finalTypeName);
  return finalTypeName;
}

void AstTree::InnerGetTypeName(const std::string& fullTypeName, std::string& finalTypeName) {
  size_t tempIndex = fullTypeName.find('<');
  if (tempIndex != std::string::npos) {
    std::string currentTypeName = fullTypeName.substr(0, tempIndex);
    finalTypeName += GetFullTypeName(currentTypeName);
    finalTypeName += '<';
    size_t tempEndIndex = fullTypeName.find_last_of('>') - 1;
    std::string leftTypeName = fullTypeName.substr(tempIndex + 1, tempEndIndex - tempIndex);
    InnerGetTypeName(leftTypeName, finalTypeName);
    finalTypeName += fullTypeName.substr(tempEndIndex + 1, -1);
  } else if (!fullTypeName.empty()) {
    finalTypeName += GetFullTypeName(fullTypeName);
  }
}

std::string AstTree::GetFullTypeName(const std::string& typeName) {
  std::string nameWithNamespace = currentNamespace + "::" + typeName;
  if (typeNameSet_.contains(nameWithNamespace))
    return nameWithNamespace;
  else
    return typeName;
}
}  // namespace Aternyx
