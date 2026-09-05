#include "Utils/ProcessUtil.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <process.h>
#define ATERNYX_POPEN _popen
#define ATERNYX_PCLOSE _pclose
#else
#include <sys/wait.h>
#include <unistd.h>
#define ATERNYX_POPEN popen
#define ATERNYX_PCLOSE pclose
#endif

namespace fs = std::filesystem;

namespace Aternyx::ProcessUtil {
namespace {

// Single quotes defeat every shell metacharacter; an embedded quote is
// closed, escaped and reopened ('\'').
std::string ShellQuote(const std::string& value) {
  std::string quoted = "'";
  for (char c : value) {
    if (c == '\'')
      quoted += "'\\''";
    else
      quoted += c;
  }
  quoted += "'";
  return quoted;
}

fs::path WriteTempInput(const std::string& content) {
  static std::atomic<int> counter{0};
#ifdef _WIN32
  const int pid = static_cast<int>(_getpid());
#else
  const int pid = static_cast<int>(getpid());
#endif
  fs::path path =
      fs::temp_directory_path() / ("aternyx_filter_" + std::to_string(pid) + "_" + std::to_string(counter++) + ".tmp");
  std::ofstream ofs(path, std::ios::binary);
  if (!ofs)
    throw std::runtime_error("post-process: cannot write temp input file " + path.string());
  ofs << content;
  ofs.close();
  return path;
}

std::string ReadAll(FILE* file) {
  std::string out;
  char buffer[4096];
  size_t read;
  while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
    out.append(buffer, read);
  return out;
}

}  // namespace

std::string RunFilter(const std::string& command, const std::string& fileName, const std::string& input) {
  // The filter reads its input from a temp file (shell redirect) and writes
  // the transformed content to stdout, which popen("r") captures.
  fs::path inputFile = WriteTempInput(input);

  std::string expanded;
  for (size_t pos = 0; pos < command.size();) {
    if (command.compare(pos, 2, "%F") == 0) {
      expanded += ShellQuote(fileName);
      pos += 2;
    } else {
      expanded += command[pos++];
    }
  }
  const std::string shellCommand = expanded + " < " + ShellQuote(inputFile.string());

  FILE* pipe = ATERNYX_POPEN(shellCommand.c_str(), "r");
  if (pipe == nullptr) {
    fs::remove(inputFile);
    throw std::runtime_error("post-process: cannot spawn command: " + command);
  }
  std::string output = ReadAll(pipe);
  const int status = ATERNYX_PCLOSE(pipe);
  fs::remove(inputFile);

  bool ok = status != -1;
#ifdef _WIN32
  ok = ok && status == 0;  // _pclose returns the exit code directly
#else
  ok = ok && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
  if (!ok)
    throw std::runtime_error("post-process command failed (exit status " + std::to_string(status) + "): " + command);
  return output;
}

}  // namespace Aternyx::ProcessUtil
