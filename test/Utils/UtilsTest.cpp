#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>

#include "Utils/Utils.h"

namespace fs = std::filesystem;

namespace {

// Guards the process-wide current directory for the duration of a test.
class ScopedCurrentPath {
 public:
  explicit ScopedCurrentPath(const fs::path& path)
      : previous_(fs::current_path()) {
    fs::current_path(path);
  }
  ~ScopedCurrentPath() { fs::current_path(previous_); }

  ScopedCurrentPath(const ScopedCurrentPath&) = delete;
  ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

 private:
  fs::path previous_;
};

// macOS exposes the temp directory through a symlink (/var -> /private/var):
// temp_directory_path() yields the logical spelling while current_path() and
// the path functions under test resolve to the physical one. Canonicalizing
// the base makes expectations agree on every platform.
fs::path PhysicalTempDir(const std::string& name) {
  const fs::path base = fs::temp_directory_path() / name;
  fs::create_directories(base);
  return fs::canonical(base);
}

}  // namespace

TEST(NormalizePathTest, CollapsesAndTrims) {
  // Absolute (drive-qualified) inputs: ".." collapses, trailing separators
  // are trimmed, mixed separators become forward slashes.
  const fs::path base = fs::temp_directory_path() / "aternyx_normalize";
  EXPECT_EQ(Aternyx::StringLib::NormalizePath((base / "src/../include/").string()),
            (base / "include").generic_string());
  EXPECT_EQ(Aternyx::StringLib::NormalizePath((base / "src").string()), (base / "src").generic_string());
  EXPECT_EQ(Aternyx::StringLib::NormalizePath((base / "src\\..\\include").string()),
            (base / "include").generic_string());
}

TEST(NormalizePathTest, ResolvesRelativeAgainstCwd) {
  const fs::path base = PhysicalTempDir("aternyx_normalize_cwd");
  ScopedCurrentPath scoped(base);

  EXPECT_EQ(Aternyx::StringLib::NormalizePath("src/a.h"), (base / "src" / "a.h").generic_string());
}

TEST(MakeIncludeSpellingTest, DeepestRootWins) {
  const fs::path base = fs::temp_directory_path() / "aternyx_spelling_deepest";
  const std::string source = (base / "src" / "a" / "b.h").generic_string();

  // Both roots contain the source; the deeper one produces the shorter spelling.
  std::string spelling = Aternyx::StringLib::MakeIncludeSpelling(
      source, (base / "build" / "gen").generic_string(), {base.generic_string(), (base / "src").generic_string()});
  EXPECT_EQ(spelling, "a/b.h");
}

TEST(MakeIncludeSpellingTest, ShallowRootKeepsSubPath) {
  const fs::path base = fs::temp_directory_path() / "aternyx_spelling_shallow";
  const std::string source = (base / "src" / "a" / "b.h").generic_string();

  std::string spelling = Aternyx::StringLib::MakeIncludeSpelling(
      source, (base / "build" / "gen").generic_string(), {base.generic_string()});
  EXPECT_EQ(spelling, "src/a/b.h");
}

TEST(MakeIncludeSpellingTest, FallsBackToReferencingDir) {
  const fs::path base = fs::temp_directory_path() / "aternyx_spelling_fallback";
  const std::string source = (base / "src" / "a.h").generic_string();
  const std::string referencing = (base / "build" / "gen").generic_string();

  // No root contains the source: the include resolves through the directory
  // of the including file, so ".." components are expected and fine.
  std::string spelling =
      Aternyx::StringLib::MakeIncludeSpelling(source, referencing, {(base / "elsewhere").generic_string()});
  EXPECT_EQ(spelling, "../../src/a.h");
}

TEST(MakeIncludeSpellingTest, UnrelatedPathsThrow) {
#if defined(_WIN32)
  // Purely lexical: different drive letters never resolve to a relative path,
  // and an empty #include "" must never be produced.
  EXPECT_THROW(Aternyx::StringLib::MakeIncludeSpelling("Z:/elsewhere/a.h", "C:/repo/build/gen", {}),
               std::runtime_error);
  EXPECT_THROW(Aternyx::StringLib::MakeIncludeSpelling("C:/repo/src/a.h", "Z:/other/build/gen", {}),
               std::runtime_error);
#else
  // POSIX has no drive roots, so two absolute paths always admit a relative
  // spelling: "unrelated" trees fall back to a ".."-based path that crosses
  // the filesystem root, which is valid there.
  EXPECT_EQ(Aternyx::StringLib::MakeIncludeSpelling("/elsewhere/a.h", "/repo/build/gen", {}),
            "../../../elsewhere/a.h");
#endif
}

TEST(MakeIncludeSpellingTest, MixedSeparatorsAreNormalized) {
  // Backslash inputs (typical Windows source paths) must yield forward
  // slashes — backslash escapes inside #include strings are UB territory.
  std::string spelling = Aternyx::StringLib::MakeIncludeSpelling("C:\\repo\\src\\a.h", "C:\\repo\\build\\gen",
                                                                 {"C:/repo"});
  EXPECT_EQ(spelling, "src/a.h");
}

TEST(MakeIncludeSpellingTest, RelativeInputsResolveAgainstCwd) {
  const fs::path base = PhysicalTempDir("aternyx_spelling_cwd");
  fs::create_directories(base / "build" / "gen");
  ScopedCurrentPath scoped(base);

  std::string spelling =
      Aternyx::StringLib::MakeIncludeSpelling("src/a.h", "build/gen", {base.generic_string()});
  EXPECT_EQ(spelling, "src/a.h");
}
