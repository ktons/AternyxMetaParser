#pragma once

#include <string>

namespace Aternyx {

// Naming style for the generated sub-directories.
// SnakeCase:  <output>/serialization, <output>/editor_ui, <output>/reflection
// CamelCase:  <output>/Serialization, <output>/EditorUi,  <output>/Reflection
enum class GenPathStyle {
  SnakeCase,
  CamelCase,
};

// Parses a style name ("snake_case" / "camel_case", case-insensitive).
// Returns false and leaves `outStyle` untouched when the name is unknown.
bool ParseGenPathStyle(const std::string& name, GenPathStyle& outStyle);

}  // namespace Aternyx
