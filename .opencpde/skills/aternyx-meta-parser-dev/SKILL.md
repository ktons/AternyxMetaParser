---
name: aternyx-meta-parser-dev
description: Build, test, and run the AternyxMetaParser tool (C++ libclang meta-parser + mustache codegen) in this repo. Use whenever the user asks to build the project, run ctest/tests, run AternyxParser.exe, generate .gen.h output, debug generated serialization code, or investigate symptoms like "field types all become int" / "cannot open include file iostream/string" — everything must run inside the MSVC (vcvars64) environment, and skipping that silently corrupts parse results.
---

# AternyxMetaParser 开发工作流

## 第一法则:一切都在 MSVC 环境里执行

构建**和运行**(包括跑测试)都必须先初始化 MSVC 开发者环境。两条原因不同,缺一不可:

1. **构建**:MSVC `cl.exe` 没有 `INCLUDE`/`LIB` 环境变量时找不到标准库头文件,直接报 `fatal error C1083: 无法打开包括文件: "iostream"`。
2. **运行/测试(更阴险)**:libclang 靠 `INCLUDE` 环境变量定位 MSVC STL 头文件。环境缺失时解析不会报错退出——字段名、注解、结构全部正常,**只有字段类型被 clang 错误恢复静默降级为 `int`**,生成代码形似神非。诊断特征:`use of undeclared identifier 'std'`、golden 测试里 `as<std::string>()` 断言失败、debug 输出里 `[name:int]`。

任何终端(Git Bash、非 VS 启动的 shell)统一用本 skill 的辅助脚本包一层:

```bash
# 用法: vs_env.bat <任意命令及其参数>
cmd //c "F:\Project\AternyxMetaParser\.agents\skills\aternyx-meta-parser-dev\scripts\vs_env.bat cmake --build F:\Project\AternyxMetaParser\build\ninja-debug-msvc"
```

脚本内部用 vswhere 自动定位 Visual Studio(回退 `F:\DevKit\Visual Studio 2026`)并调用 `vcvars64.bat`,再执行传入的命令。直接在 VS Developer PowerShell/命令提示符里工作时不需要这层包装。

## 构建

```bash
# CMake preset: ninja-debug-msvc / ninja-release-msvc / ninja-debug-clang / ninja-release-clang
cmake --build build/ninja-debug-msvc
```

产物:`build/bin/AternyxParser.exe`(主工具)、`build/bin/AternyxParserTests.exe`(测试)。

## 测试

```bash
ctest --test-dir build/ninja-debug-msvc --output-on-failure
```

- 共 19 个用例,由 `gtest_discover_tests` 注册;`WORKING_DIRECTORY` 固定为仓库根,测试靠 `fs::current_path()` 定位 `example/`、`Template/`。
- 单跑一个:`ctest --test-dir build/ninja-debug-msvc -R VerifySerializationContent --output-on-failure`,或直接跑 exe(必须 cwd=仓库根):`./build/bin/AternyxParserTests.exe --gtest_filter=CodeGeneratorTest.VerifySerializationContent`。
- **golden 测试 `CodeGeneratorTest.VerifySerializationContent` 是字段准确性的唯一防线**:解析 example → 生成 → 子串断言 `_generated_test/serialization/user_struct.gen.h` 的字段名/类型/Runtime 过滤/editor_ui 命中。它失败时先怀疑 MSVC 环境(见上),再看产物内容。
- 测试 `TearDown` 会删除 `_generated_test/`;想人工看产物,用主工具生成到别的目录(见下)。

## 运行解析器

```bash
# cwd 建议为仓库根;-p 传项目根,否则生成文件的 #include 行为空 "#include \"\""
build/bin/AternyxParser.exe example/main.cpp -o _generated -t Template -i example -p .
# cmake 模式:分析编译数据库各 target 的 include 路径;--target 对指定 target 生成
build/bin/AternyxParser.exe --cmake <项目>/build/compile_commands.json -o _gen_report
build/bin/AternyxParser.exe --cmake <项目>/build/compile_commands.json --target <目标名> -o <输出> -t Template -p <项目源根> -i <项目源根> --gen-path-style camel_case
```

参数:`--codegen`(默认)codegen 模式,`<source_file>` 主源文件;`--cmake` cmake 模式,`<input>` 为 compile_commands.json 或其目录,`--target` 指定生成目标(缺省只出报告);`-o` 输出目录(默认 `_generated`);`-t` 模板目录;`-i` include 路径(一次可传多个,cmake 模式下作为 target 路径的补充);`-p` 项目根;`--gen-path-style {snake_case,camel_case}` 生成子目录风格(默认 snake_case);`--toml` 配置文件。输出落到 `<o>/serialization|editor_ui|reflection/*.gen.h`(camel_case 时为 `Serialization|EditorUi|Reflection`)。解析错误(缺 include 等)会抛异常并以非 0 退出码结束。

## 速查

- 功能全景、模板清单、已知限制:**`docs/MetaParser.md`**(读它,别重新考古)。
- 标注宏:`meta/meta_attributes.h`(`CLASS/STRUCT/ENUM_CLASS/META`,展开为 annotate attribute)。
- 解析核心:`source/Parser/Parser.cpp`(作用域感知递归收集);生成核心:`source/CodeGenerator/CodeGenerator.cpp`(模板注册表 `kTempConfigList`,属性匹配大小写不敏感)。
- 模板:`Template/*.mustache`;示例输入:`example/user_struct.h`。
