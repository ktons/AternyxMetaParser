#pragma once

#include <string>
#include <vector>

#include "meta/meta_attributes.h"

namespace Mini {
STRUCT(Entity, Serialization) {
  META() int id;
  std::string name;
  META(Serializable) std::vector<float> values;
};

STRUCT(Component, Serialization) {
  META() int slot;
  std::string tag;
};
}  // namespace Mini
