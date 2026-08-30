#include "Parser/MetaInfo.h"

#include <algorithm>
#include <cctype>
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

std::string AstTree::GetTypeName(const std::string& typeName) const {
  // Rewrite every identifier token of the spelling through the registry,
  // leaving all punctuation and decorations (`<`, `,`, `*`, `&`, `const`,
  // array brackets, ...) untouched. Reading `ident(::ident)*` as one token
  // keeps `std::vector` from being split into `std` + `vector`.
  std::string result;
  const size_t size = typeName.size();
  auto isIdentStart = [](char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
  };
  auto isIdentChar = [&](char c) { return isIdentStart(c) || std::isdigit(static_cast<unsigned char>(c)); };
  size_t i = 0;
  while (i < size) {
    if (isIdentStart(typeName[i])) {
      size_t end = i + 1;
      while (end < size && isIdentChar(typeName[end]))
        ++end;
      // Extend over namespace qualifiers: `::` must be followed by an
      // identifier for the token to continue.
      while (end + 1 < size && typeName[end] == ':' && typeName[end + 1] == ':' && end + 2 < size &&
             isIdentStart(typeName[end + 2])) {
        end += 2;
        while (end < size && isIdentChar(typeName[end]))
          ++end;
      }
      result += ResolveRegisteredTypeName(typeName.substr(i, end - i));
      i = end;
    } else {
      result += typeName[i];
      ++i;
    }
  }
  return result;
}

namespace {

std::string SimpleNameOf(const std::string& name) {
  const size_t qualifierEnd = name.rfind("::");
  return qualifierEnd == std::string::npos ? name : name.substr(qualifierEnd + 2);
}

std::vector<std::string> SplitComponents(const std::string& name) {
  std::vector<std::string> components;
  size_t pos = 0;
  while (!name.empty()) {
    const size_t next = name.find("::", pos);
    components.push_back(name.substr(pos, next == std::string::npos ? std::string::npos : next - pos));
    if (next == std::string::npos)
      break;
    pos = next + 2;
  }
  return components;
}

// Number of leading `::`-separated components `prefix` and `name` share.
size_t SharedNamespaceDepth(const std::string& prefix, const std::string& name) {
  const std::vector<std::string> prefixComponents = SplitComponents(prefix);
  const std::vector<std::string> nameComponents = SplitComponents(name);
  size_t depth = 0;
  while (depth < prefixComponents.size() && depth < nameComponents.size() &&
         prefixComponents[depth] == nameComponents[depth])
    ++depth;
  return depth;
}

}  // namespace

std::string AstTree::ResolveRegisteredTypeName(const std::string& name) const {
  std::string cacheKey = currentNamespace;
  cacheKey += '@';
  cacheKey += name;
  if (auto it = resolutionCache_.find(cacheKey); it != resolutionCache_.end())
    return it->second;

  const std::string resolved = [this, &name]() {
    // 1) Known exactly as written (fully qualified, std::, or builtin).
    if (typeNameSet_.contains(name))
      return name;

    // 2) Enclosing namespaces of the declaring type, innermost first —
    //    normal C++ name hiding: a shadowing type wins over a distant one.
    if (!currentNamespace.empty()) {
      const std::vector<std::string> components = SplitComponents(currentNamespace);
      for (size_t depth = components.size(); depth >= 1; --depth) {
        std::string candidate;
        for (size_t c = 0; c < depth; ++c) {
          if (c > 0)
            candidate += "::";
          candidate += components[c];
        }
        candidate += "::";
        candidate += name;
        if (typeNameSet_.contains(candidate))
          return candidate;
      }
    }

    const std::string simpleName = SimpleNameOf(name);

    // 3) Registered names ending in the written spelling (partial
    //    qualification, e.g. `Resource::AssetMetaInfo` for
    //    `Aternyx::Resource::AssetMetaInfo`).
    std::vector<std::string> candidates;
    for (const auto& registered : typeNameSet_) {
      if (registered.size() > name.size() && registered.compare(registered.size() - name.size(), name.size(), name) == 0 &&
          registered[registered.size() - name.size() - 1] == ':')
        candidates.push_back(registered);
    }
    // 4) Fall back to the simple name.
    if (candidates.empty()) {
      for (const auto& registered : typeNameSet_) {
        if (SimpleNameOf(registered) == simpleName)
          candidates.push_back(registered);
      }
    }
    if (!candidates.empty()) {
      std::sort(candidates.begin(), candidates.end());
      if (candidates.size() == 1)
        return candidates.front();
      // Ambiguous simple name: the candidate sharing the longest namespace
      // prefix with the declaring type wins; ties go to the lexicographically
      // first (candidates are sorted, so strictly-greater keeps the first).
      std::string best = candidates.front();
      const size_t bestDepth = SharedNamespaceDepth(currentNamespace, best);
      for (const auto& candidate : candidates) {
        const size_t depth = SharedNamespaceDepth(currentNamespace, candidate);
        if (depth > bestDepth)
          best = candidate;
      }
      std::cerr << "[AstTree] warning: ambiguous type name '" << name << "' in namespace '"
                << (currentNamespace.empty() ? "::" : currentNamespace) << "', resolved to '" << best << "' (candidates:";
      for (const auto& candidate : candidates)
        std::cerr << " '" << candidate << "'";
      std::cerr << ")" << std::endl;
      return best;
    }

    // 5) Not a project type (std::, uint64_t, ...) — keep as written.
    return name;
  }();

  resolutionCache_.emplace(std::move(cacheKey), resolved);
  return resolved;
}
}  // namespace Aternyx
