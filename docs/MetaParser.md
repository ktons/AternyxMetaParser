# AternyxMetaParser 功能说明

> 本文档描述项目**当前实际具备**的能力(2026-08 验证),与 README 的快速上手互补。

## 1. 项目定位

AternyxMetaParser 是一个基于 **libclang + Mustache 模板**的 C++ 元数据解析与代码生成工具:

```
C++ 源码(标注宏) ──libclang──▶ MetaStruct/MetaField(AST 元数据)
                                    │
                                    ▼
                        CodeGenerator(按注解匹配模板)
                                    │
                                    ▼
              _generated/{serialization, editor_ui, reflection}/*.gen.h
```

- 序列化目标格式:yaml-cpp(生成 `YAML::convert<T>` 特化,提供 `encode`/`decode`)。
- 参考实现:[AustinBrunkhorst/CPP-Reflection](https://github.com/AustinBrunkhorst/CPP-Reflection)。

## 2. 标注语法(meta/meta_attributes.h)

仅在解析模式(`__REFLECTION_PARSER__`,由解析器自动注入)下展开为 `__attribute__((annotate(...)))`,对编译器无副作用:

| 宏 | 展开效果 | 说明 |
|---|---|---|
| `CLASS(名字, 属性...)` | 类 + 注解 | 属性逗号分隔,可多个 |
| `STRUCT(名字, 属性...)` | 结构体 + 注解 | 同上 |
| `ENUM_CLASS(名字, ...)` | enum class + 固定 `EnumCast` 注解 | ⚠️ 指定的底层类型会被丢弃,生成端硬编码 `uint32_t` |
| `META(属性...)` | 给字段/方法加注解 | 如 `META(Runtime) int scratch;` |

当前生成端识别的注解(匹配**大小写不敏感**,逗号分隔后会去首尾空格):

- 类型级:`Serialization`、`EnumCast`、`EditorUi`/`EditorUI`、`ObjectHandleSerialization`、`Variant`、`VisitEditorUi`、`CustomUi`
- 字段级:`Runtime`(该字段不参与生成)、`Serializable`(成员方法参与 function_list,见 §6 限制)

## 3. 解析能力(Parser)

`MetaParser` 用 libclang 遍历整个翻译单元(含 include 的头文件),采用**作用域感知的递归收集**:

- 收集 `StructDecl / ClassDecl / EnumDecl` 的定义(前置声明仅用于注册类型名,辅助字段类型补全);
- 提取:类型注解、基类(`CXXBaseSpecifier`)、字段(`FieldDecl`)、成员方法(`CXXMethod`)、字段级注解;
- 字段类型名经 `AstTree::GetTypeName` 规范化(注册表 + 命名空间补全);
- 入树条件:有注解且有字段,或为枚举;无注解的类型只注册名字、不入树;
- 修复记录:旧实现(扁平状态机)会丢失"每个子树最后一个类型"(曾被 `VerifyDataBlockFields` 测试实证),现版本收集完成立即入树。

## 4. 代码生成(CodeGenerator)

模板注册表(`source/CodeGenerator/CodeGenerator.cpp` 的 `kTempConfigList`):

| 模板 | 优先级 | 输出 | 触发方式 |
|---|---|---|---|
| `EnumCast.mustache` | TYPE_A | `enum_cast.gen.h`(全局一份) | 聚合所有标 `EnumCast` 的枚举 |
| `Serialization.mustache` | TYPE_B | `<源文件名>.gen.h` | 按源文件分组,聚合该文件中标 `Serialization` 的类型 |
| `ObjectHandleSerialization.mustache` | TYPE_B | `<源文件名>_object_handle.gen.h` | 同上 |
| `EditorUi.mustache` | TYPE_B | `<源文件名>.gen.h`(editor_ui/) | 标 `EditorUi` 的类型 |
| `VisitEditorUi.mustache` | TYPE_C | `<源文件名>_visit_ui.gen.h` | 派生类型遍历 |
| `Variant.mustache` | TYPE_B | `<源文件名>_variant.gen.h`(reflection/) | 标 `Variant` 的类型 |
| `_AllInclude.mustache` | TYPE_E | `all_include.gen.h` | 自动聚合已生成文件 |

输出目录:`<output>/<serialization|editor_ui|reflection>/`。生成文件头部会注入预设 include(如 serialization 注入 `<yaml-cpp/yaml.h>`)。

**序列化生成示例**(`example/user_struct.h` → `serialization/user_struct.gen.h`):

```cpp
template <>
struct convert<UserStruct::ClassA> {
  static Node encode(const UserStruct::ClassA & v) {
    Node node;
    node["k"] = v.k;
    node["name"] = v.name;
    node["lengthList_"] = v.lengthList_;
    return node;
  }
  static bool decode(const Node& node, UserStruct::ClassA & v) {
    if (!node["k"].IsNull())
      v.k = node["k"].as<int>();
    if (!node["name"].IsNull())
      v.name = node["name"].as<std::string>();
    if (!node["lengthList_"].IsNull())
      v.lengthList_ = node["lengthList_"].as<std::vector<float>>();
    return true;
  }
};
```

字段名、字段类型、`Runtime` 字段过滤均准确;嵌套结构体靠 `YAML::convert` 特化的链式查找生效。

## 5. 运行环境要求(重要)

**必须在 Visual Studio 开发者环境(或设置了 `INCLUDE`/`LIB` 环境变量)下运行解析器与测试。**

原因:libclang 依赖 `INCLUDE` 环境变量定位 MSVC STL/Windows SDK 头文件。**环境缺失时解析会显式失败**:libclang 报出 `fatal error: 'string' file not found` 等诊断,解析器抛出 `MetaParseError` 异常,进程返回非 0(2x 版起,clang 错误诊断一律抛异常,不再静默继续)。

- 解析标准:`-std=c++17` 为默认;cmake 模式下自动采用 target 在编译数据库中声明的标准(如 `c++latest` 会映射为 `c++26`,因为 libclang 的 GNU driver 不识别 MSVC 拼写)。
- 自动化终端中可用 `vcvars64.bat` 包一层,例如:
  ```bat
  call "...\VC\Auxiliary\Build\vcvars64.bat" && cmake --build build\ninja-debug-msvc && ctest --test-dir build\ninja-debug-msvc
  ```

### 诊断行为

`MetaParser::BuildCursor` 收集 libclang 全部诊断:

- **Error 及以上** → 抛 `Aternyx::MetaParseError`(继承 `std::runtime_error`),消息含每条 `文件:行:列` 诊断;生成流程 fail-fast。
- **Warning** → 打印到 stderr,不中断。
- 这保证"include 解析失败 / 字段类型被错误恢复降级"这类问题**第一时间暴露**,不会静默产出形似神非的生成代码。

## 6. 测试(test/,GTest,共 19 个)

- 由 `gtest_discover_tests` 注册到 ctest,`WORKING_DIRECTORY` 为仓库根(测试通过 `fs::current_path()` 定位 `example/`、`Template/`)。
- 覆盖:Cursor 封装、ArgConfig、libclang 冒烟、Parser 字段解析(`VerifyClassAFields`、`VerifyDataBlockFields`)、CodeGenerator 端到端。
- **golden 内容测试 `CodeGeneratorTest.VerifySerializationContent`**:解析 example → 生成 → 对 `_generated_test/serialization/user_struct.gen.h` 做子串断言(字段名、`as<类型>()`、`convert<全限定名>`、Runtime 字段被排除),并校验 editor_ui 输出。这是唯一守住"字段准确"的防线。
- 注意:测试只断言生成文本,不编译生成代码。

## 7. CLI 用法

工具支持两种模式,用互斥开关选择,缺省为 codegen:

```
AternyxParser.exe [--codegen] <source_file> [选项]      # 解析单个源文件并生成
AternyxParser.exe --cmake <compile_commands.json|目录> [--target <名>] [选项]
```

通用选项:

- `-o` 输出目录(默认 `_generated`);cmake 报告模式下报告 JSON 也写到这里。
- `-t` 模板目录;`-p` 项目根(生成文件 `#include "相对路径"` 依赖它)。
- `-i` include 路径,可一次传多个(`-i path1 path2`);codegen 模式直接作为解析路径;cmake 模式追加到 target 自身路径之后(例如补传项目根,让生成文件里的 `#include "Runtime/..."` 可解析)。
- `--gen-path-style {snake_case, camel_case}` 生成子目录命名风格,默认 snake_case(`serialization/`、`editor_ui/`、`reflection/`),camel_case 为 `Serialization/`、`EditorUi/`、`Reflection/`。
- `--toml` 配置文件:键可写在文件根或 `[parserParams]` 表下,支持 `output_path`、`project_path`、`template_path`、`include_paths`、`target`、`gen_path_style`;显式给出的命令行选项优先于 TOML。
- 任何解析错误都会以异常终结并以非 0 退出码结束(见 §5 诊断行为)。

### --cmake 模式

基于 CMake 编译数据库(`compile_commands.json`,需 Ninja 生成器 + `CMAKE_EXPORT_COMPILE_COMMANDS=ON`):

1. **分析报告**(不带 `--target`):按 `output` 字段中的 `CMakeFiles/<target>.dir/` 把每条编译记录归入 target,收集各 target 的源文件、include 路径(`-I`/`/I`/`-isystem`,**原样采用不做过滤**)、`-D` 宏定义与语言标准;控制台打印报告,并写 `<output>/cmake_targets_report.json`。
2. **target 级生成**(`--target <名>`):对该 target 的全部 .cpp,用其 include 路径/宏/标准逐个解析,合并去重后一次生成到 `-o` 目录。任一文件解析失败即抛异常终止。

典型用法(在项目根、MSVC 环境下):

```bat
AternyxParser.exe --cmake build\compile_commands.json -o _gen_report
AternyxParser.exe --cmake build\compile_commands.json --target NyxCoreUtils -o Source\_Generated -t <模板目录> -p Source -i Source --gen-path-style camel_case
```

**bootstrap 注意**:若源码直接 `#include` 生成物(如 `_Generated/Serialization/X.gen.h`),首次生成时该文件尚不存在会报 include 错误。两种处理:先创建空占位文件(生成时会被真实内容覆盖),或先以定义该类型的头文件作为 codegen 模式的输入生成首版。另外,`YAML::convert<T>` 特化链(T 的成员也需特化时)要求所有相关 gen.h 在实例化点可见——让源码 include `all_include.gen.h` 即可聚合全部生成文件。

## 8. 已知限制(未实现/有缺陷,改造前勿依赖)

1. **成员方法序列化模板有误**:`Serialization.mustache` 对 `function_list` 生成 `node["x"] = v.x;`(取函数指针,无法编译),decode 完全忽略方法。
2. **枚举底层类型硬编码 `uint32_t`**,且枚举常量名不解析;`ENUM_CLASS` 宏丢弃指定的底层类型。
3. **类型系统薄弱**:无类型分类(map/optional/智能指针/数组未处理);类型名规范化是字符串启发式,多参数模板内层不逐个补全;const/指针/引用未处理。不支持的类型会原样进模板,依赖 yaml-cpp 内建转换,失败时生成代码编译不过且生成器无校验。
4. **属性只有无值标签**,不支持 `rename/skip/default` 等 key=value。
5. 模板类(`CXCursor_ClassTemplate`)、函数内/`extern "C"` 内的类型不收集。
6. `EditorUi.mustache` 无条件写 `v.is_dirty = true`(类型需有该成员);`VisitEditorUi.mustache` 硬编码 include 路径;`ObjectHandleSerialization.mustache` 的 decode 有误。
7. 生成结果不校验合法性(不编译、无 schema 检查)。

## 9. 目录速览

```
meta/          标注宏定义
example/       解析示例(测试输入)
Template/      mustache 模板
source/        Parser(libclang 封装+收集)、CodeGenerator、Config、Utils、CMakeAnalyzer(编译数据库分析+target 级生成)
test/          GTest(与 source/ 结构镜像)
docs/          本文档
```
