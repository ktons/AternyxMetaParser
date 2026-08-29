
# AternyxMetaParser 简易说明 / Quick Guide

## 依赖 Dependencies

- [llvm/llvm-project (libclang)](https://github.com/llvm/llvm-project)
- [kainjow/Mustache](https://github.com/kainjow/Mustache)

## 简介 Introduction

这是一个 C++ 元数据解析和代码生成的初步版本。它可以扫描你的 C++ 源码，结合 `Template/` 下的 mustache 模板，自动生成代码到 `_Generated/` 目录。

> 当前功能的完整说明(解析能力、生成模板、运行环境要求、已知限制)见 [docs/MetaParser.md](docs/MetaParser.md)。

---

## 如何使用 How to Use

1. **编译项目 Build the Project**
   - CMakePreset + MSVC
   - 需要安装LLVM以获取libclang.dll, 注意添加到PATH
   - 推荐使用 `Developr PowerShell for VS` 打开目录，然后 `Code .` 用VSCode打开
   - 然后 `CMake --build --preset ninja-debug --target AternyxParser` 编译即可

2. **运行解析 Run the Parser**
   - codegen 模式(默认,解析单个源文件并生成):
     ```
     .\build\bin\AternyxParser.exe .\example\main.cpp -o _Generated -t Template -i example -p .
     ```
   - cmake 模式(分析编译数据库中各 target 的 include 路径,或对指定 target 生成):
     ```
     .\build\bin\AternyxParser.exe --cmake build\compile_commands.json -o _gen_report
     .\build\bin\AternyxParser.exe --cmake build\compile_commands.json --target <目标名> -o _Generated -t Template -p . --gen-path-style camel_case
     ```
     需要先以 Ninja + `CMAKE_EXPORT_COMPILE_COMMANDS=ON` 配置生成 compile_commands.json。
   - `--gen-path-style` 可切换生成子目录风格(snake_case 默认 / camel_case)。

3. **查看生成结果 Check Output**
   - 查看 `Template/` 目录，了解有哪些模板可用。
   - 运行后，自动生成的代码会在输出目录的 `serialization|Serialization`、`editor_ui|EditorUi`、`reflection|Reflection` 子目录下。
   - 解析报错(如 include 失败)会直接抛异常并以非 0 退出码结束,不会静默生成错误代码。

---
