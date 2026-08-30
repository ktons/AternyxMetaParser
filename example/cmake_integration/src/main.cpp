#include <cstdio>

#include "my_types.h"

int main() {
  PlayerState state;
  state.name = "aternyx";
  state.health = 100;
  std::printf("%s: %d\n", state.name.c_str(), state.health);
  return 0;
}

// To consume the generated serializers, include the category aggregator from
// the generated include root (build/generated) once the runtime headers it
// depends on (e.g. yaml-cpp) are available to your project:
//
//   #if __has_include("serialization/all_include.gen.h")
//   #include "serialization/all_include.gen.h"
//   #endif
//
// The __has_include guard keeps the very first build green: the generated
// files do not exist yet when it starts, and code generation runs as part of
// that same build (see aternyx_target_codegen()).
