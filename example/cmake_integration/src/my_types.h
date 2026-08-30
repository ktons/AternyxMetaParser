#pragma once

#include <string>

#include "meta/meta_attributes.h"

// Annotated types are declared in a self-contained header: it compiles on
// its own and never includes generated files. The generated code lands in
// <build>/generated/serialization/player_state.gen.h after the build.
STRUCT(PlayerState, Serialization) {
  META() std::string name;
  META() int health;
  // Runtime fields are excluded from serialization.
  META(Runtime) int scratch;
};
