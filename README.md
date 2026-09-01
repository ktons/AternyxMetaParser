
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
   唯一入口是编译数据库(compile_commands.json):分析各 target 的 include 路径,或对指定 target 生成:
   ```
   .\build\bin\AternyxParser.exe build\compile_commands.json -o _gen_report
   .\build\bin\AternyxParser.exe build\compile_commands.json --target <目标名> -o _Generated -t Template
   ```
   需要先以 Ninja + `CMAKE_EXPORT_COMPILE_COMMANDS=ON` 配置生成 compile_commands.json。
   - `--toml <配置>`:工程无关配置——`gen_path_style`(生成子目录风格,snake_case 默认/CamelCase)、`parse_headers`、`header_markers`,见 `example/params.toml`。
   - `parse_headers`(.h-as-source):只解析注解头文件(文本预筛 `CLASS(`/`STRUCT(`/`ENUM_CLASS(`),不解析 .cpp。约定:头文件自包含、不 include 生成物——首跑永不因生成物缺失而失败。

3. **查看生成结果 Check Output**
   - 查看 `Template/` 目录，了解有哪些模板可用。
   - 运行后，自动生成的代码会在输出目录的 `serialization|Serialization`、`editor_ui|EditorUi`、`reflection|Reflection` 子目录下。
   - 生成文件自动 include 其依赖类型的生成物（gen→gen 依赖），字段类型自动全限定——无需手工修补即可编译。
   - 解析报错(如 include 失败)会直接抛异常并以非 0 退出码结束,不会静默生成错误代码。

4. **接入构建 CMake Integration**
   用 `aternyx_target_codegen()` 把 codegen 挂进构建:编译前自动生成,输出目录自动加入 target 的 include 路径(输出路径与 include 路径单点决定):
   ```cmake
   include(<本仓库>/cmake/AternyxMetaParser.cmake)
   aternyx_target_codegen(my_target
     PARSER <AternyxParser可执行文件>
     TEMPLATE_DIR <本仓库>/Template
     CONFIG <工程无关配置.toml>)   # gen_path_style / parse_headers / header_markers
   ```
   完整可运行示例见 [example/cmake_integration/](example/cmake_integration/)。

---
