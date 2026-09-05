#pragma once

#include <filesystem>
#include <functional>
#include <mustache.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Config/GenPathStyle.h"
#include "Parser/MetaInfo.h"

namespace Aternyx {

// Built-in category names. Categories are open strings: custom registrations
// (CodegenConfig.categories) may introduce their own.
inline constexpr std::string_view kCategorySerialization = "serialization";
inline constexpr std::string_view kCategoryEditorUi = "editor_ui";
inline constexpr std::string_view kCategoryReflection = "reflection";

// Output sub-directory names of one category, one per path style.
struct CodegenCategory {
  std::string name;
  std::string snakeDir;
  // Empty: snakeDir is used for both styles.
  std::string camelDir;
};

// How a registered template produces output files.
enum class OutputKind {
  // One output per source file declaring matching annotated types
  // ("<stem><outputPattern>", e.g. user_struct.gen.h).
  PerSourceFile,
  // One output for the whole run aggregating every matching type
  // (outputPattern is the full name, e.g. enum_cast.gen.h).
  GlobalAggregate,
  // Marker for planned aggregator outputs (see CodegenAggregator). Not a
  // valid TemplateDesc::outputKind — aggregators are registered separately.
  CategoryAggregator,
};

// One template registry entry. The template name doubles as the annotation
// name that triggers generation (matched case-insensitively, so an annotation
// `CLASS(Foo, Serialization)` renders Serialization.mustache); the file
// "<name>.mustache" must exist in the template directory — a missing file
// only warns, which makes registry entries double as an opt-out switch.
struct TemplateDesc {
  // Author casing; also the file name stem.
  std::string name;
  // Category deciding the output sub-directory and the aggregator that lists
  // the outputs. Must be registered in CodegenConfig::categories.
  std::string category;
  OutputKind outputKind{OutputKind::PerSourceFile};
  // Sort key within a category (ascending): orders GlobalAggregate outputs
  // and the include list of the category aggregator.
  int order{100};
  // Appended to the source file stem for PerSourceFile ("<stem><pattern>"),
  // the full file name otherwise.
  std::string outputPattern;
  // Optional shell filter run over this template's rendered outputs
  // (content on stdin, transformed content on stdout; "%F" expands to the
  // output file name). The CodeGenerator itself ignores it: RunTargetCodegen
  // implements it as a transformOutput step (the CLI exposes it via the TOML
  // `post_process` key). Empty = none.
  std::string postProcess;
};

// Aggregates every planned output of one category into a single file — the
// generalized all_include.gen.h. Its template data is one list,
// `include_file_list`: the category's prelude entries (CodegenConfig::preludes)
// followed by the planned outputs sorted by (order, file name). The aggregator
// is written into the category's own directory, so bare file names are the
// correct spellings. Removing the built-in entries disables allinclude
// generation; custom categories can register their own.
struct CodegenAggregator {
  // Category whose outputs are aggregated.
  std::string category;
  // Template to render (e.g. "_AllInclude"); a leading "_" keeps it from
  // colliding with a real annotation name.
  std::string templateName;
  // e.g. "all_include.gen.h".
  std::string outputName;
};

// Built-in registry: the three known categories, the stock templates and one
// all_include aggregator per category.
std::vector<CodegenCategory> DefaultCodegenCategories();
std::vector<TemplateDesc> DefaultTemplates();
std::vector<CodegenAggregator> DefaultAggregators();

// Explicit configuration for code generation. Keeps CodeGenerator free of
// global singletons so it can be driven programmatically (e.g. one run per
// CMake target with its own output directory).
struct CodegenConfig {
  // Root directory all generated files are written into.
  std::string outputPath;
  // Directory holding the mustache templates.
  std::string templatePath;
  // Candidate include roots used to spell the `#include` lines referencing
  // the analyzed headers (see StringLib::MakeIncludeSpelling): the deepest
  // root containing a header wins — correct by construction when the roots
  // are the include directories of the target that compiles the generated
  // files. When no root contains a header, the include falls back to a path
  // relative to the generated file's own directory.
  std::vector<std::string> includeRoots;
  // Per parsed source file: every file it transitively #includes (normalized
  // absolute paths). Used to emit `gen_include_list` entries for generated
  // outputs of included annotated headers — generated code references the
  // generated specializations of the types it uses, which live in those
  // outputs, not in the (already included) source headers.
  std::unordered_map<std::string, std::vector<std::string>> sourceIncludes;
  // Naming style of the generated sub-directories.
  GenPathStyle pathStyle{GenPathStyle::SnakeCase};
  // Registry overrides. An empty list seeds the corresponding built-in
  // defaults (DefaultCodegenCategories / DefaultTemplates / DefaultAggregators),
  // so plain callers keep the stock behavior. Non-empty lists REPLACE the
  // built-ins entirely — mix the Default*() entries back in to extend rather
  // than replace (e.g. keep the stock templates and add a custom category).
  std::vector<CodegenCategory> categories;
  std::vector<TemplateDesc> templates;
  std::vector<CodegenAggregator> aggregators;
  // Per-category prelude includes injected at the top of the category's
  // aggregator output. Deliberately empty by default: engine headers are an
  // upper-layer concern (the CLI installs its own defaults in Main.cpp).
  std::unordered_map<std::string, std::vector<std::string>> preludes;
};

// One planned generation. The full output identity is resolved at planning
// time so hooks (transformJobs) can rewrite it and every downstream artifact
// — the output registry feeding `gen_include_list`, and the aggregators —
// follows the rewritten values.
struct GenJob {
  // Lowercased template name (the registry lookup key).
  std::string templateName;
  // Category deciding the output sub-directory and which aggregator lists
  // this output.
  std::string category;
  OutputKind outputKind{OutputKind::PerSourceFile};
  // Final output file name: "<stem><outputPattern>" (PerSourceFile) or
  // "<outputPattern>" (GlobalAggregate / aggregator jobs).
  std::string outputName;
  // Source file stem (empty for GlobalAggregate and aggregator jobs).
  std::string fileName;
  // Declaring source file path (empty for GlobalAggregate and aggregator
  // jobs).
  std::string sourceFile;
  std::vector<MetaStruct*> metaStructList;
};

// Optional behavior injection points along the codegen pipeline — the C++
// half of the extension model (the data half is CodegenConfig's registry).
// Every hook is optional: an empty std::function keeps the built-in
// behavior, and state lives in the lambda captures. Hooks may throw to
// fail the whole generation.
struct CodegenHooks {
  // Type filter, applied when the AST is consumed: a type for which it
  // returns false takes part in no planning, rendering or aggregation.
  std::function<bool(const MetaStruct&)> acceptType;
  // Called after the built-in MetaStruct -> template-data mapping; may add
  // or override fields of the type's mustache context (e.g. a computed
  // `base_type_name`). Runs once per type, before any rendering.
  std::function<void(const MetaStruct&, kainjow::mustache::data&)> decorateTypeData;
  // Called once after planning, before any output is registered or
  // rendered: may add, remove or rewrite jobs — including their category /
  // outputName. Jobs added here must reference registered templates. The
  // output registry that feeds `gen_include_list` and the aggregators is
  // derived AFTER this hook returns, so rewrites are honored everywhere.
  std::function<void(std::vector<GenJob>&)> transformJobs;
  // Called with every rendered output (aggregator files included) just
  // before it is written. Returns the content to write, or std::nullopt to
  // skip writing this output.
  std::function<std::optional<std::string>(const GenJob&, std::string content)> transformOutput;
};

class CodeGenerator {
 public:
  CodeGenerator();
  ~CodeGenerator();
  // Same as Init(config, CodegenHooks{}) — for call sites without hooks.
  void Init(const CodegenConfig& config);
  // Loads `config.templates` (empty: the built-in defaults) from
  // `config.templatePath` and validates the registry: unknown categories,
  // duplicate template names and dangling aggregator references throw
  // std::runtime_error. A template file that exists but is not valid
  // mustache throws as well; missing files only warn on stderr (the entry
  // is skipped — an opt-out switch).
  void Init(const CodegenConfig& config, CodegenHooks hooks);
  void SetAstTree(AstTree* astTree);
  // Plans the generation jobs, applies hooks.transformJobs, registers the
  // planned outputs, then renders every job (hooks.transformOutput runs per
  // output just before writing) — aggregator jobs included, rendered last.
  void Run();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  // A planned output referenced from `gen_include_list` template data.
  struct GenOutputRef {
    std::string fileName;  // e.g. "user_struct.gen.h"
    std::string dir;       // directory the file is written into
  };

  CodegenConfig config_;
  CodegenHooks hooks_;
  // Effective registry: config lists, or the built-in defaults when empty.
  std::vector<CodegenCategory> categories_;
  std::vector<TemplateDesc> templates_;
  std::vector<CodegenAggregator> aggregators_;
  // Registry entries by lowercased template name -> index into templates_
  // (every entry, loaded or not), and the loaded mustache bodies keyed the
  // same way — annotation templates plus aggregator templates. A registered
  // entry whose file is missing has a templateIndex_ but no body (it warns
  // and acts as an opt-out).
  std::unordered_map<std::string, size_t> templateIndex_;
  std::unordered_map<std::string, kainjow::mustache::mustache> loadedTemplates_;
  AstTree* astTree_{nullptr};
  std::vector<GenJob> jobs_;
  // Planned outputs per declaring source file (PerSourceFile jobs only;
  // keyed by the normalized source file path).
  std::unordered_map<std::string, std::vector<GenOutputRef>> genFilesBySource_;
  std::unordered_map<MetaStruct*, kainjow::mustache::data> metaStructDataMap_;

  void InitMetaStructGroup();
  void CreateMetaStructData(MetaStruct* metaStruct);

  bool GetAttribute(const std::vector<std::string>& attributes, const std::string& attribute);

  // Builds the raw `jobs_` from the AST (GlobalAggregate jobs first, then
  // the per-source-file jobs). No output registration here: that happens in
  // RegisterOutputs, after hooks.transformJobs had its say.
  void PlanJobs();
  // Pre-registers every planned output (so later-rendered files can
  // reference earlier-planned ones), fills the per-category aggregate lists
  // and appends one job per registered aggregator whose category has
  // outputs.
  void RegisterOutputs();
  // Renders one job, computing `include_file_list` (source-header spellings)
  // and `gen_include_list` (sibling generated outputs of the same source
  // file); aggregator jobs render their category's aggregate include list.
  void RenderJob(const GenJob& job);

  std::filesystem::path ResolveOutputDir(const std::string& category) const;
};
}  // namespace Aternyx
