#include "Config/GenPathStyle.h"

#include "Utils/Utils.h"

namespace Aternyx {

bool ParseGenPathStyle(const std::string& name, GenPathStyle& outStyle) {
  const std::string lowered = StringLib::ToLower(StringLib::Trim(name));
  if (lowered == "snake_case") {
    outStyle = GenPathStyle::SnakeCase;
    return true;
  }
  if (lowered == "camel_case") {
    outStyle = GenPathStyle::CamelCase;
    return true;
  }
  return false;
}

}  // namespace Aternyx
