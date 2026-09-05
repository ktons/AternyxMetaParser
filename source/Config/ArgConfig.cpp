#include "Config/ArgConfig.h"

#include <argparse.hpp>
#include <iostream>
#include <toml.hpp>

#include "Utils/Utils.h"

template <typename T>
inline void ApplyParserValue(T& value, const argparse::ArgumentParser& parser, const std::string& paramsName) {
  auto params = parser.get<T>(paramsName);
  if (!params.empty())
    value = params;
}

template <typename T>
inline void ApplyParserValue(std::vector<T>& value,
                             const argparse::ArgumentParser& parser,
                             const std::string& paramsName) {
  auto params = parser.get<std::vector<T>>(paramsName);
  for (auto& elem : params) {
    value.emplace_back(elem);
  }
}

namespace {

// Required string field of a TOML table: fills `out` and returns true on
// success; reports and returns false when the key is missing or not a string.
bool RequireString(const toml::table& tbl, const char* key, std::string& out, const std::string& context) {
  const toml::node* node = tbl.get(key);
  if (node == nullptr) {
    std::cerr << "TOML: " << context << " is missing required string key '" << key << "'" << std::endl;
    return false;
  }
  auto value = node->value<std::string>();
  if (!value) {
    std::cerr << "TOML: " << context << " key '" << key << "' must be a string" << std::endl;
    return false;
  }
  out = *value;
  return true;
}

// Optional string field: fills `out` only when present; reports and returns
// false when present but not a string.
bool OptionalString(const toml::table& tbl, const char* key, std::string& out, const std::string& context) {
  const toml::node* node = tbl.get(key);
  if (node == nullptr)
    return true;
  auto value = node->value<std::string>();
  if (!value) {
    std::cerr << "TOML: " << context << " key '" << key << "' must be a string" << std::endl;
    return false;
  }
  out = *value;
  return true;
}

}  // namespace

bool ArgConfig::Validate() {
  if (compileDbPath_.empty()) {
    std::cerr << "No compile_commands.json path provided." << std::endl;
    return false;
  }
  return true;
}

void ArgConfig::DebugInfo() {
  std::cout << "output path: " << outputPath_ << std::endl;
  std::cout << "gen path style: " << (genPathStyle_ == Aternyx::GenPathStyle::CamelCase ? "CamelCase" : "snake_case")
            << std::endl;
  std::cout << "target: " << (target_.empty() ? "<report only>" : target_) << std::endl;
}

bool ArgConfig::ParseArgs(int argc, char* argv[]) {
  const std::string progName = (argc > 0 && argv[0]) ? argv[0] : "AternyxParser";
  argparse::ArgumentParser parser(progName);

  parser.add_argument("input")
      .help("compile_commands.json path (or its directory)")
      .nargs(1)
      .store_into(compileDbPath_);

  parser.add_argument("-o", "--output-path").help("Generator output path").default_value(std::string("_generated"));

  parser.add_argument("-p", "--project-path")
      .help("Extra header scan root for parse_headers mode")
      .default_value(std::string(""));

  parser.add_argument("-t", "--template-path").help("Template directory path").default_value(std::string("template"));

  parser.add_argument("-i", "--include-path").help("Include paths").nargs(argparse::nargs_pattern::any);

  parser.add_argument("--toml")
      .help("TOML config file path (gen_path_style / parse_headers / header_markers)")
      .default_value(std::string{""});

  parser.add_argument("--target")
      .help("run code generation for this target instead of only reporting")
      .default_value(std::string{""});

  try {
    parser.parse_args(argc, const_cast<const char* const*>(argv));
  } catch (const std::runtime_error& err) {
    std::cerr << "Error parsing arguments: " << err.what() << std::endl;
    return false;
  }

  // TOML values are applied first, then only explicitly given CLI options
  // overwrite them (TOML still wins over built-in defaults). A broken config
  // file aborts instead of silently running on defaults.
  auto tomlPath = parser.get<std::string>("--toml");
  if (!tomlPath.empty() && !ParseTomlConfig(tomlPath.data()))
    return false;

  if (parser.is_used("-o"))
    outputPath_ = parser.get<std::string>("-o");
  if (parser.is_used("-t"))
    templatePath_ = parser.get<std::string>("-t");
  if (parser.is_used("-p"))
    projectPath_ = parser.get<std::string>("-p");
  if (parser.is_used("-i")) {
    includePaths_.clear();
    ApplyParserValue(includePaths_, parser, "-i");
  }

  target_ = parser.get<std::string>("--target");

  return Validate();
}

bool ArgConfig::ParseTomlConfig(const char* tomlConfigPath) {
  if (tomlConfigPath == nullptr || *tomlConfigPath == '\0') {
    std::cerr << "Empty TOML config path provided." << std::endl;
    return false;
  }

  try {
    auto res = toml::parse_file(tomlConfigPath);
    toml::table tbl = res;

    if (auto node = tbl.get("gen_path_style"))
      if (auto v = node->value<std::string>()) {
        Aternyx::GenPathStyle style = Aternyx::GenPathStyle::SnakeCase;
        if (!Aternyx::ParseGenPathStyle(*v, style)) {
          std::cerr << "Unknown gen_path_style in TOML: " << *v
                    << Aternyx::StringLib::ToLower(Aternyx::StringLib::Trim(*v)).c_str() << std::endl;
          return false;
        }
        genPathStyle_ = style;
      }

    if (auto node = tbl.get("parse_headers"))
      if (auto v = node->value<bool>())
        parseHeaders_ = *v;

    if (auto node = tbl.get("header_markers"))
      if (auto arr = node->as_array()) {
        headerMarkers_.clear();
        for (auto& elem : *arr) {
          if (auto s = elem.value<std::string>())
            headerMarkers_.push_back(*s);
        }
      }

    // --- Codegen registry extensions ---
    // [[codegen_category]]: register a category or override a built-in by
    // name (e.g. to change its sub-directory spellings).
    if (auto node = tbl.get("codegen_category"))
      if (auto arr = node->as_array()) {
        for (auto& elem : *arr) {
          const toml::table* entry = elem.as_table();
          if (entry == nullptr) {
            std::cerr << "TOML: codegen_category entries must be tables" << std::endl;
            return false;
          }
          const std::string context = "codegen_category[" + std::to_string(codegenCategories_.size()) + "]";
          Aternyx::CodegenCategory category;
          if (!RequireString(*entry, "name", category.name, context))
            return false;
          if (!OptionalString(*entry, "snake_dir", category.snakeDir, context))
            return false;
          if (category.snakeDir.empty())
            category.snakeDir = category.name;
          if (!OptionalString(*entry, "camel_dir", category.camelDir, context))
            return false;
          codegenCategories_.push_back(std::move(category));
        }
      }

    // [[codegen_template]]: register a template or override a built-in by
    // name; `remove = true` drops the named built-in instead (an opt-out).
    if (auto node = tbl.get("codegen_template"))
      if (auto arr = node->as_array()) {
        for (auto& elem : *arr) {
          const toml::table* entry = elem.as_table();
          if (entry == nullptr) {
            std::cerr << "TOML: codegen_template entries must be tables" << std::endl;
            return false;
          }
          const std::string context = "codegen_template[" + std::to_string(codegenTemplates_.size()) + "]";
          std::string name;
          if (!RequireString(*entry, "name", name, context))
            return false;
          if (const toml::node* removeNode = entry->get("remove");
              removeNode != nullptr && removeNode->value<bool>().value_or(false)) {
            removedTemplates_.push_back(Aternyx::StringLib::ToLower(name));
            continue;
          }
          Aternyx::TemplateDesc desc;
          desc.name = name;
          if (!RequireString(*entry, "category", desc.category, context))
            return false;
          if (!RequireString(*entry, "output", desc.outputPattern, context))
            return false;
          if (!OptionalString(*entry, "post_process", desc.postProcess, context))
            return false;
          if (const toml::node* kindNode = entry->get("kind")) {
            auto kind = kindNode->value<std::string>();
            if (!kind) {
              std::cerr << "TOML: " << context << " key 'kind' must be a string" << std::endl;
              return false;
            }
            if (*kind == "per_source") {
              desc.outputKind = Aternyx::OutputKind::PerSourceFile;
            } else if (*kind == "global") {
              desc.outputKind = Aternyx::OutputKind::GlobalAggregate;
            } else {
              std::cerr << "TOML: " << context << " unknown kind '" << *kind
                        << "' (expected \"per_source\" or \"global\")" << std::endl;
              return false;
            }
          }
          if (const toml::node* orderNode = entry->get("order")) {
            auto order = orderNode->value<int64_t>();
            if (!order) {
              std::cerr << "TOML: " << context << " key 'order' must be an integer" << std::endl;
              return false;
            }
            desc.order = static_cast<int>(*order);
          }
          // Other templates whose outputs this template's gen_include_list
          // may reference (see TemplateDesc::genIncludeDeps).
          if (const toml::node* depsNode = entry->get("gen_include_deps")) {
            const toml::array* depsArr = depsNode->as_array();
            if (depsArr == nullptr) {
              std::cerr << "TOML: " << context << " key 'gen_include_deps' must be an array of template names"
                        << std::endl;
              return false;
            }
            for (auto& elem : *depsArr) {
              if (auto s = elem.value<std::string>())
                desc.genIncludeDeps.push_back(*s);
            }
          }
          codegenTemplates_.push_back(std::move(desc));
        }
      }

    // [[codegen_aggregator]]: register the allinclude aggregator of a
    // category or override the built-in one by category; `remove = true`
    // disables allinclude generation for the category.
    if (auto node = tbl.get("codegen_aggregator"))
      if (auto arr = node->as_array()) {
        for (auto& elem : *arr) {
          const toml::table* entry = elem.as_table();
          if (entry == nullptr) {
            std::cerr << "TOML: codegen_aggregator entries must be tables" << std::endl;
            return false;
          }
          const std::string context = "codegen_aggregator[" + std::to_string(codegenAggregators_.size()) + "]";
          std::string category;
          if (!RequireString(*entry, "category", category, context))
            return false;
          if (const toml::node* removeNode = entry->get("remove");
              removeNode != nullptr && removeNode->value<bool>().value_or(false)) {
            removedAggregators_.push_back(category);
            continue;
          }
          Aternyx::CodegenAggregator aggregator;
          aggregator.category = category;
          if (!RequireString(*entry, "template", aggregator.templateName, context))
            return false;
          if (!RequireString(*entry, "output", aggregator.outputName, context))
            return false;
          codegenAggregators_.push_back(std::move(aggregator));
        }
      }

    // [codegen_prelude.<category>]: replaces the tool's engine prelude for
    // that category wholesale (an explicit empty `includes = []` clears it).
    if (auto node = tbl.get("codegen_prelude"))
      if (auto preludeTbl = node->as_table()) {
        for (auto&& [key, value] : *preludeTbl) {
          const toml::table* entry = value.as_table();
          if (entry == nullptr) {
            std::cerr << "TOML: codegen_prelude." << key.str() << " must be a table with an 'includes' array"
                      << std::endl;
            return false;
          }
          std::vector<std::string> includes;
          if (const toml::node* includesNode = entry->get("includes")) {
            const toml::array* includesArr = includesNode->as_array();
            if (includesArr == nullptr) {
              std::cerr << "TOML: codegen_prelude." << key.str() << " key 'includes' must be an array" << std::endl;
              return false;
            }
            for (auto& elem : *includesArr) {
              if (auto s = elem.value<std::string>())
                includes.push_back(*s);
            }
          }
          codegenPreludes_[std::string(key.str())] = std::move(includes);
        }
      }

    return true;
  } catch (const std::exception& e) {
    std::cerr << "Exception while parsing TOML: " << e.what() << std::endl;
    return false;
  }
}
