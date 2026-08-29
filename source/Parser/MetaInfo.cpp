#include "Parser/MetaInfo.h"

#include <algorithm>
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

void AstTree::MergeFrom(AstTree&& other) {
  // Map other-tree indices to this tree: duplicates of already-collected
  // types resolve to the existing entry, new types shift by the offset.
  std::unordered_map<uint32_t, uint32_t> remap;
  std::vector<MetaStruct> newStructs;
  for (uint32_t i = 0; i < other.metaStructList.size(); ++i) {
    MetaStruct& candidate = other.metaStructList[i];
    auto existing = metaStructMap_.find(candidate.typeName);
    if (existing != metaStructMap_.end()) {
      remap.emplace(i, existing->second);
      continue;
    }
    remap.emplace(i, static_cast<uint32_t>(metaStructList.size() + newStructs.size()));
    newStructs.push_back(std::move(candidate));
    metaStructMap_[newStructs.back().typeName] = remap.at(i);
  }

  // Point the moved structs' derived-type indices at their new positions.
  for (MetaStruct& appended : newStructs) {
    for (uint32_t& derivedIndex : appended.derivedTypeIndex) {
      auto mapped = remap.find(derivedIndex);
      if (mapped != remap.end())
        derivedIndex = mapped->second;
    }
  }

  const size_t offset = metaStructList.size();
  for (MetaStruct& appended : newStructs) {
    metaStructList.push_back(std::move(appended));
  }
  newStructs.clear();

  // A derived type may live in the other tree while its base was collected
  // earlier here; re-establish those links (skipping existing ones).
  for (size_t i = offset; i < metaStructList.size(); ++i) {
    MetaStruct& derived = metaStructList[i];
    if (derived.baseTypeName.empty())
      continue;
    auto baseIt = metaStructMap_.find(derived.baseTypeName);
    if (baseIt == metaStructMap_.end())
      continue;
    MetaStruct& base = metaStructList[baseIt->second];
    const uint32_t derivedIndex = static_cast<uint32_t>(i);
    if (std::find(base.derivedTypeIndex.begin(), base.derivedTypeIndex.end(), derivedIndex) ==
        base.derivedTypeIndex.end())
      base.derivedTypeIndex.push_back(derivedIndex);
  }

  typeNameSet_.insert(other.typeNameSet_.begin(), other.typeNameSet_.end());
  other.metaStructMap_.clear();
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
