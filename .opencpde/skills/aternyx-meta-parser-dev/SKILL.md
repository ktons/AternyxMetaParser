---
name: aternyx-meta-parser-dev
description: Build, test, and run the AternyxMetaParser tool (C++ libclang meta-parser + mustache codegen) in this repo. Use whenever the user asks to build the project, run ctest/tests, run AternyxParser.exe, generate .gen.h output, debug generated serialization code, or investigate symptoms like "field types all become int" / "cannot open include file iostream/string" — everything must run inside the MSVC (vcvars64) environment, and skipping that silently corrupts parse results.
---

# AternyxMetaParser 开发工作流

## 第一法则:一切都在 MSVC 环境里执行

构建**和运行**(包括跑测试)都必须先初始化 MSVC 开发者环境。两条原因不同,缺一不可:

1. **构建**:MSVC `cl.exe` 没有 `INCLUDE`/`LIB` 环境变量时找不到标准库头文件,直接报 `fatal error C1083: 无法打开包括文件: "iostream"`。
2. **运行/测试(更阴险)**:libclang 靠 `INCLUDE` 环境变量定位 MSVC STL 头文件。环境缺失时解析不会报错退出——字段名、注解、结构全部正常,**只有字段类型被 clang 错误恢复静默降级为 `int`**,生成代码形似神非。诊断特征:`use of undeclared identifier 'std'`、golden 测试里 `as<std::string>()` 断言失败、debug 输出里 `[name:int]`。

任何终端(Git Bash、非 VS 启动的 shell)统一用仓库的辅助脚本包一层:

```bash
# 用法: vs_run.bat <任意命令及其参数>
cmd //c "F:\Project\AternyxMetaParser\build\vs_run.bat cmake --build F:\Project\AternyxMetaParser\build\ninja-debug-msvc"
```

脚本内部调用 `vcvars64.bat`(VS 位于 `F:\DevKit\Visual Studio 2026`)初始化环境,再执行传入的命令。`build/` 下还有 `run_build.bat`(构建)、`run_test.bat`(ctest)两个现成入口。直接在 VS Developer PowerShell/命令提示符里工作时不需要这层包装。

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

- 共 66 个用例,由 `gtest_discover_tests` 注册;`WORKING_DIRECTORY` 固定为仓库根,测试靠 `fs::current_path()` 定位 `example/`、`Template/`。
- 单跑一个:`ctest --test-dir build/ninja-debug-msvc -R VerifySerializationContent --output-on-failure`,或直接跑 exe(必须 cwd=仓库根):`./build/bin/AternyxParserTests.exe --gtest_filter=CodeGeneratorTest.VerifySerializationContent`。
- **golden 测试 `CodeGeneratorTest.VerifySerializationContent` 是字段准确性的唯一防线**:解析 example → 生成 → 子串断言 `_generated_test/serialization/user_struct.gen.h` 的字段名/类型/Runtime 过滤/editor_ui 命中。它失败时先怀疑 MSVC 环境(见上),再看产物内容。
- 测试 `TearDown` 会删除 `_generated_test/`;想人工看产物,用主工具生成到别的目录(见下)。

## 运行解析器

唯一入口是编译数据库(compile_commands.json);没有手动单文件模式:

```bash
# 分析编译数据库各 target 的 include 路径(缺省只出报告)
build/bin/AternyxParser.exe <项目>/build/compile_commands.json -o _gen_report
# --target 对指定 target 生成
build/bin/AternyxParser.exe <项目>/build/compile_commands.json --target <目标名> -o <输出> -t Template
```

参数:`<input>` 为 compile_commands.json 或其目录;`--target` 指定生成目标(缺省只出报告);`-o` 输出目录(默认 `_generated`);`-t` 模板目录;`-i` include 路径(一次可传多个,作为 target 路径的补充,同时是生成文件 `#include` 拼写的候选根);`-p` parse_headers 模式的附加扫描根;`--toml` 工程无关配置文件(`gen_path_style` 生成子目录风格、`parse_headers` 解析注解头、`header_markers` 注解头预筛标记,见 `example/params.toml`)。输出落到 `<o>/serialization|editor_ui|reflection/*.gen.h`(camel_case 时为 `Serialization|EditorUi|Reflection`)。解析错误(缺 include 等)会抛异常并以非 0 退出码结束。

## 速查

- 功能全景、模板清单、已知限制:**`docs/MetaParser.md`**(读它,别重新考古)。
- 标注宏:`meta/meta_attributes.h`(`CLASS/STRUCT/ENUM_CLASS/META`,展开为 annotate attribute)。
- 解析核心:`source/Parser/Parser.cpp`(作用域感知递归收集;含 TU 包含文件收集 `CollectInclusions`);生成核心:`source/CodeGenerator/CodeGenerator.cpp`(模板注册表 `kTempConfigList`,属性匹配大小写不敏感)。
- 模板:`Template/*.mustache`;示例输入:`example/user_struct.h`。

## 生成行为要点(2026-08-30)

- **gen→gen 依赖自动 include**:源文件(传递)包含的注解头若有生成物,生成文件自动 include 之(`gen_include_list`,libclang `clang_getInclusions` 收集)。相关:`Parser.cpp` 的 `CollectInclusions` → `TargetCodegen.cpp` 的 `sourceIncludes` → `CodeGenerator.cpp` 的 `RenderJob`。
- **字段类型自动全限定**:生成代码里的项目类型全部带命名空间(`as<Aternyx::Guid>()`),实现见 `MetaInfo.cpp` 的 `AstTree::ResolveRegisteredTypeName`(注册表五级解析,歧义取最长命名空间前缀+stderr 警告)。
- 改动生成逻辑后,对真实项目验证:用 `--target <名>` 生成到临时目录 diff,别直接覆盖项目 `_Generated`。

## libclang 已知坑(踩过的)

- `clang_getInclusions` **必须**在 `CXTranslationUnit_DetailedPreprocessingRecord` 下调用(本仓库经 `clang_parseTranslationUnit2` 传该标志;`clang_createTranslationUnitFromSourceFile` 无 options 参数),否则静默返回空甚至读野指针。
- 该 API 的回调 clientData 类型必须与实际传入对象严格一致——曾把 `std::set` 强转成 `std::vector*` 调 `emplace_back`,堆损坏以 SEH 0xc0000005 崩在测试里,且无该标志时 visitor 不被调用、问题被完全掩盖。
- 回调也会以空 inclusion stack(len==0)报告**主文件本身**,收集时需跳过。
