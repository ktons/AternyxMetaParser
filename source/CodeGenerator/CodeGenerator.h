#pragma once

#include <filesystem>
#include <mustache.hpp>
#include <unordered_map>
#include <vector>

#include "Config/GenPathStyle.h"
#include "Parser/MetaInfo.h"

namespace Aternyx {

enum class TempType {
  REFLECTION,
  SERIALIZATION,
  EDITOR_UI,
  NONE,
};

// Maps the configuration category names ("serialization", "editor_ui",
// "reflection") to TempType. Returns false for unknown names.
bool ParseTempTypeCategory(const std::string& name, TempType& out);

// Explicit configuration for code generation. Keeps CodeGenerator free of
// global singletons so it can be driven programmatically (e.g. one run per
// CMake target with its own output directory).
struct CodegenConfig {
  // Root directory all generated files are written into.
  std::string outputPath;
  // Directory holding the mustache templates.
  std::string templatePath;
  // Legacy project root. No longer used for include spellings (see
  // includeRoots); kept for configuration compatibility.
  std::string projectPath;
  // Candidate include roots used to spell the `#include` lines referencing
  // the analyzed headers (see StringLib::MakeIncludeSpelling): the deepest
  // root containing a header wins — correct by construction when the roots
  // are the include directories of the target that compiles the generated
  // files. When no root contains a header, the include falls back to a path
  // relative to the generated file's own directory.
  std::vector<std::string> includeRoots;
  // Per-category prelude includes injected into the category's
  // all_include.gen.h. A present entry (even an empty one) replaces the
  // built-in defaults for that category; absent entries keep the defaults.
  std::unordered_map<TempType, std::vector<std::string>> preIncludes;
  // Naming style of the generated sub-directories.
  GenPathStyle pathStyle{GenPathStyle::SnakeCase};
};

class CodeGenerator {
 public:
  CodeGenerator();
  ~CodeGenerator();
  // Loads the templates listed in `kTempConfigList` from `config.templatePath`.
  // Throws std::runtime_error when a template file exists but is not valid
  // mustache. Missing template files are optional and only warn on stderr.
  void Init(const CodegenConfig& config);
  void SetAstTree(AstTree* astTree);
  void Run();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  // One planned generation: all types annotated with `tempName` that were
  // declared in `sourceFile` (empty for whole-run aggregates like EnumCast).
  struct GenJob {
    std::string tempName;
    std::string fileName;
    std::string sourceFile;
    std::vector<MetaStruct*> metaStructList;
  };

  // A planned output referenced from `gen_include_list` template data.
  struct GenOutputRef {
    std::string fileName;  // e.g. "user_struct.gen.h"
    std::string dir;       // directory the file is written into
  };

  CodegenConfig config_;
  std::unordered_map<std::string, int> tempMap_;
  std::vector<kainjow::mustache::mustache> tempList_;
  AstTree* astTree_{nullptr};
  std::vector<GenJob> jobs_;
  // Planned outputs per declaring source file (per-file templates only).
  std::unordered_map<std::string, std::vector<GenOutputRef>> genFilesBySource_;
  std::unordered_map<MetaStruct*, kainjow::mustache::data> metaStructDataMap_;

  void InitMetaStructGroup();
  void CreateMetaStructData(MetaStruct* metaStruct);

  bool GetAttribute(const std::vector<std::string>& attributes, const std::string& attribute);

  // Builds `jobs_` from the AST, pre-registers every planned output (so
  // later-rendered files can reference earlier-planned ones) and fills the
  // per-category aggregate lists used by GenAllIncludes.
  void PlanJobs();
  // Renders and writes one job, computing `include_file_list` (source-header
  // spellings) and `gen_include_list` (sibling generated outputs of the same
  // source file).
  void RenderJob(const GenJob& job);
  // Writes the per-category all_include.gen.h aggregators.
  void GenAllIncludes();

  std::filesystem::path ResolveOutputDir(TempType tempType) const;
  // config_.preIncludes entry for the category, or the built-in defaults.
  const std::vector<std::string>& PreludeFor(TempType tempType) const;
};
}  // namespace Aternyx
