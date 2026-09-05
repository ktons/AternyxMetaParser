# AternyxMetaParser 设计与实现

> 本文档描述项目的**设计理念与具体实现**(2026-09 验证),与 README 的快速上手互补。
> 想定制/扩展 codegen(模板层 + C++ 层)请看 `CodegenExtension.md`。

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

### 设计理念

实现中反复出现的几条不变量,读代码和改代码前先记住它们:

- **fail-fast**:解析诊断 Error 即抛异常、模板语法错误即抛异常、注册表校验失败即抛异常——绝不静默产出"形似神非"的生成代码(见 §5 诊断行为)。
- **可编程驱动**:`CodeGenerator`/`RunTargetCodegen` 均无全局单例依赖,一切输入走显式配置结构(`CodegenConfig`/`TargetCodegenOptions`),库可以被上层程序直接驱动(测试即是范例)。
- **依赖方向**:引擎知识(yaml-cpp/imgui 头、allinclude prelude)不进通用库——库默认 preludes 为空,由 CLI 层(`Main.cpp`)注入(见 §4、`CodegenExtension.md` §2.5)。
- **单一事实来源**:生成输出目录同时是该 target 的 include 目录,生成文件的 `#include` 拼写按构造可解析(见 §4"include 拼写策略")。
- **扩展 = 数据 + 行为分离**:数据型定制(注册表)可配置,行为型定制(钩子)走 C++,配置文件永远不携带代码(见 `CodegenExtension.md`)。

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
- 注解集合是开放的:匹配不到任何注册模板的注解被忽略,上层可注册自定义模板引入新注解(见 `CodegenExtension.md` §2)

## 3. 解析能力(Parser)

`MetaParser` 用 libclang 遍历整个翻译单元(含 include 的头文件),采用**作用域感知的递归收集**:

- 收集 `StructDecl / ClassDecl / EnumDecl` 的定义(前置声明仅用于注册类型名,辅助字段类型补全);
- 提取:类型注解、基类(`CXXBaseSpecifier`)、字段(`FieldDecl`)、成员方法(`CXXMethod`)、字段级注解;
- 字段类型名经 `AstTree::GetTypeName` **全限定重写**(见下);
- 入树条件:有注解且有字段,或为枚举;无注解的类型只注册名字、不入树;
- 修复记录:旧实现(扁平状态机)会丢失"每个子树最后一个类型"(曾被 `VerifyDataBlockFields` 测试实证),现版本收集完成立即入树。

### 字段类型全限定(2026-08-30)

生成代码活在陌生命名空间语境里(如 `namespace YAML`),源码里的简写类型(`Guid`、`std::vector<Guid>`)直接照搬会编译失败。现版本对字段/基类类型拼写做**标识符 token 级重写**:只把项目内类型(已注册全限定名的)替换为全限定拼写,`const`/`*`/`&`/数组括号/模板标点原样保留,`uint64_t`、`std::string` 等非项目类型不动。解析顺序:

1. 拼写本身已是注册全限定名 → 原样;
2. 声明类型的命名空间链(由内到外,保持 C++ 名字遮蔽语义);
3. 注册表中以该拼写为后缀的名字(部分限定,如 `Resource::AssetMetaInfo`);
4. 简单名在注册表唯一 → 直接用;多候选 → 取与声明命名空间共享前缀最长者(并列取字典序,stderr 警告),确定性可编译;
5. 均未命中 → 原样保留。

模板实参逐个解析(`std::vector<Guid>` → `std::vector<Aternyx::Guid>`)。TU 内先于字段访问到的类型(含 include 带入的)都会注册,靠值使用的字段所需类型必然可见。

## 4. 代码生成(CodeGenerator)

模板注册表已开放为 `CodegenConfig` 数据(内置默认见 `source/CodeGenerator/CodeGenerator.cpp` 的 `DefaultTemplates()`/`DefaultAggregators()`/`DefaultCodegenCategories()`,即原 `kTempConfigList`):

| 模板 | 类别 | 输出方式 | 输出 | 触发方式 |
|---|---|---|---|---|
| `EnumCast.mustache` | serialization | 全局一份(order 0) | `enum_cast.gen.h` | 聚合所有标 `EnumCast` 的枚举 |
| `Serialization.mustache` | serialization | 按源文件 | `<源文件名>.gen.h` | 按源文件分组,聚合该文件中标 `Serialization` 的类型 |
| `ObjectHandleSerialization.mustache` | serialization | 按源文件 | `<源文件名>_object_handle.gen.h` | 同上 |
| `EditorUi.mustache` | editor_ui | 按源文件 | `<源文件名>.gen.h`(editor_ui/) | 标 `EditorUi` 的类型 |
| `VisitEditorUi.mustache` | editor_ui | 按源文件(order 200) | `<源文件名>_visit_ui.gen.h` | 派生类型遍历 |
| `Variant.mustache` | reflection | 按源文件 | `<源文件名>_variant.gen.h`(reflection/) | 标 `Variant` 的类型 |
| `_AllInclude.mustache` | 每类别一个聚合器 | 聚合器 | `all_include.gen.h` | 自动聚合已生成文件 |

要点:

- **注解名即模板名**:类型注解(大小写不敏感)匹配注册表里的模板名,匹配不到的注解被忽略——所以自定义模板不需要改解析端(怎么加见 `CodegenExtension.md` §2)。
- **类别是开放字符串**:内置 serialization/editor_ui/reflection 三类决定生成子目录(`kSubDirNames` 的 snake/Camel 两套拼写现在是 `CodegenCategory` 数据);自定义类别随意注册。
- **allinclude 是普通注册项**(`CodegenAggregator`,每类别一条):从注册表移除即禁用某类别的 allinclude;自定义类别可以注册自己的聚合器。聚合器头部注入该类别的 **prelude**(prelude 已从库中移出,是 `CodegenConfig::preludes` 配置数据,库默认为空——见 `CodegenExtension.md` §2.5)。
- 模板文件缺失只警告并跳过该条目(可当禁用开关);模板语法错误或注册表校验失败(未知类别、重名)直接抛异常。

输出目录:`<output>/<serialization|editor_ui|reflection|...>/`。每类目录下的 `all_include.gen.h` 头部注入该类别的 prelude include(见下文"include 拼写策略"与 `CodegenExtension.md` §2);单文件生成头不注入 prelude,约定先 include `all_include.gen.h` 再使用。

### include 拼写策略(2026-08 重构)

生成文件对源头文件的 `#include` 拼写不再依赖 `-p` 猜测,规则为:

1. **db 反推**:在解析所用 include 根(`-i` 列表,cmake 模式下即 target 在 compile db 中的 `-I`)里,取"包含该头文件的最深根",拼写 = 相对它的路径。因为生成文件由同一 target 编译,这些根按构造存在于消费方编译中,**拼出的 include 必然可解析**。
2. **相对兜底**:头文件不在任何根下时,拼写 = 相对生成文件自身目录(利用编译器对 `""` include 先搜包含者所在目录的规则,零配置可解析)。
3. 全程正斜杠(修复 Windows 反斜杠进 `#include` 的转义隐患);路径根无关(如跨盘)时直接报错,绝不产出 `#include ""`。

`-p/--project-path` 不参与 include 拼写:拼写根必须来自消费编译真实存在的根,否则会产出无法解析的 include。`-p` 仅作为 `parse_headers` 模式的附加扫描根。`gen_include_list`(gen→gen 依赖)也由生成器按相对本文件目录拼写,模板中不再有硬编码生成路径。

### gen→gen 依赖(include 图,2026-08-30)

生成代码引用的 `convert<T>` 等特化不在源头头文件里,而在**其依赖类型的生成物**里。因此生成器现在自动维护生成物之间的 include:

- 每个源文件解析时用 `clang_getInclusions` 收集其**传递包含**的全部头文件;
- 某生成文件的 `gen_include_list` = 同源兄弟产物 ∪ 「源文件(传递)包含的头文件中,凡有生成产物的」那些产物;排除自身,按相对本文件目录拼写(跨类别自动 `../`)。

效果:源文件 include 了带注解的头(直接或经由公共头),其生成物自动 include 对应生成物——`AssetMetaInfo.gen.h` 自动带上 `Guid.gen.h`,不再要求手工 include `all_include.gen.h` 来满足特化链可见性。实现注意:`clang_getInclusions` 必须在 `CXTranslationUnit_DetailedPreprocessingRecord` 下才有数据(见 `Parser.cpp`)。

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

字段名、字段类型(自动全限定)、`Runtime` 字段过滤均准确;嵌套结构体靠 `YAML::convert` 特化的链式查找生效。生成文件头部还有 `gen_include_list` 段自动带入其依赖的生成物(见上文"gen→gen 依赖")。

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

## 6. 测试(test/,GTest,经 ctest 注册共 86 个用例)

- 由 `gtest_discover_tests` 注册到 ctest,`WORKING_DIRECTORY` 为仓库根(测试通过 `fs::current_path()` 定位 `example/`、`Template/`)。
- 覆盖:Cursor 封装、ArgConfig(编译数据库参数 + TOML 键含 codegen 注册键)、libclang 冒烟、Parser 字段解析(`VerifyClassAFields`、`VerifyDataBlockFields`、字段类型全限定 `QualifiesFieldTypeNames`)、CodeGenerator 端到端、include 拼写策略(`UtilsTest`、`IncludeSpelling*`、`GenIncludeList*`)、target 级生成(`TargetCodegenTest`,含 `parse_headers`、gen→gen 依赖 `GeneratedOutputIncludesOutputsOfIncludedHeaders`)、扩展面(`CodegenExtensionTest`:注册表定制、prelude、四个钩子;`TargetCodegenTest` 的 `ParseTargetAst*`/`RunTargetCodegenAppliesHooks`/post_process)。
- **golden 内容测试 `CodeGeneratorTest.VerifySerializationContent`**:解析 example → 生成 → 对 `_generated_test/serialization/user_struct.gen.h` 做子串断言(字段名、`as<类型>()`、`convert<全限定名>`、Runtime 字段被排除),并校验 editor_ui 输出。这是唯一守住"字段准确"的防线。
- 注意:测试只断言生成文本,不编译生成代码。

## 7. CLI 用法

唯一的运行入口是 CMake 编译数据库:

```
AternyxParser.exe <compile_commands.json|目录> [--target <名>] [选项]
```

选项:

- `-o` 输出目录(默认 `_generated`);报告模式下报告 JSON 也写到这里。
- `-t` 模板目录。
- `-p` `parse_headers` 模式的附加头文件扫描根(**不参与 include 拼写**;拼写根自动取自 target 的 `-I`)。
- `-i` include 路径,可一次传多个(`-i path1 path2`);追加到 target 自身路径之后。**`-i` 同时是生成文件 `#include` 拼写的候选根**(见上文"include 拼写策略"),把哪些根传进来,生成文件的 include 就以哪个为根。
- `--target <名>` 对该 target 生成;缺省只输出分析报告。
- `--toml` 配置文件:工程无关设置,**只读根级键**——`gen_path_style`(生成子目录命名风格,`snake_case` 默认/`CamelCase`)、`parse_headers`(解析注解头而非 .cpp)、`header_markers`(注解头文本预筛标记),以及 codegen 注册键(`[[codegen_category]]`/`[[codegen_template]]`/`[[codegen_aggregator]]`/`[codegen_prelude.<类别>]`,见 `CodegenExtension.md` §2);见 `example/params.toml` 与 `example/custom_templates/`。TOML 值覆盖内置默认,命令行选项始终优先。
- 任何解析错误(含配置文件错误)都会以异常终结并以非 0 退出码结束(见 §5 诊断行为)。

### 运行模式

基于 CMake 编译数据库(`compile_commands.json`,需 Ninja 生成器 + `CMAKE_EXPORT_COMPILE_COMMANDS=ON`):

1. **分析报告**(不带 `--target`):按 `output` 字段中的 `CMakeFiles/<target>.dir/` 把每条编译记录归入 target,收集各 target 的源文件、include 路径(`-I`/`/I`/`-isystem`,**原样采用不做过滤**)、`-D` 宏定义与语言标准;控制台打印报告,并写 `<output>/cmake_targets_report.json`。
2. **target 级生成**(`--target <名>`):对该 target 的全部 .cpp(或,`parse_headers` 开启时,其注解头文件),用其 include 路径/宏/标准逐个解析,合并去重后一次生成到 `-o` 目录。任一文件解析失败即抛异常终止。

3. **`parse_headers`(.h-as-source)**:不再解析 .cpp,改为扫描 target 各 .cpp 所在目录(递归,可加 `-p` 附加根)下的 `.h/.hpp/.hxx/.hh`,**文本预筛**含注解标记(默认 `CLASS(`、`STRUCT(`、`ENUM_CLASS(`,可用 TOML `header_markers` 覆盖)的头文件后逐个解析。使用约定:

   - **头文件自包含**:单独立编译必须通过(否则报错,消息中会说明该约定);
   - **头文件不 include 生成物**、注解只写在头文件里(.cpp 内注解的类型不会被收集);
   - 好处:解析输入与生成物消费者(.cpp)是两个不相交的集合,首次生成永远不因生成物缺失而失败;一个头只解析一次,天然去重。

   典型用法(在项目根、MSVC 环境下;`aternyx_parser.toml` 内 `parse_headers = true`、`gen_path_style = "CamelCase"`):

   ```bat
   AternyxParser.exe build\compile_commands.json --target NyxCoreUtils -o Source\_Generated -t Template --toml aternyx_parser.toml
   ```

典型用法(在项目根、MSVC 环境下):

```bat
AternyxParser.exe build\compile_commands.json -o _gen_report
AternyxParser.exe build\compile_commands.json --target NyxCoreUtils -o Source\_Generated -t Template -i Source
```

**bootstrap / 首跑安全**:若手写代码要 include 生成物,推荐 `__has_include` 守卫,首次构建(生成物尚不存在)自动跳过,后续构建正常生效:

```cpp
#if __has_include("_Generated/Serialization/all_include.gen.h")
#include "_Generated/Serialization/all_include.gen.h"
#endif
```

配合 `parse_headers` 时,源 .cpp include 生成物完全不影响解析(解析只碰头文件)。`YAML::convert<T>` 特化链(T 的成员也需特化时)现在由生成器自动满足:生成文件自带对其依赖生成物的 include(见 §4"gen→gen 依赖")。若手写代码想一次拿到全部生成文件,仍可 include `all_include.gen.h`。

**CMake 集成(推荐,也是唯一推荐的接入方式)**:仓库提供 `cmake/AternyxMetaParser.cmake` 的 `aternyx_target_codegen(<target> ...)` 函数,把上述流程挂进构建:编译前自动跑 codegen(stamp 依赖:目标源文件 + compile_commands.json + 解析器),并把输出目录加入该 target 的 include 路径——"输出路径"与"include 路径"在同一个地方决定。完整示例见 `example/cmake_integration/`。

```cmake
include(<AternyxMetaParser仓库>/cmake/AternyxMetaParser.cmake)
aternyx_target_codegen(my_target
  PARSER <AternyxParser可执行文件>          # 也可以是 aternyx_add_parser_tool 构建的自定义解析工具
  TEMPLATE_DIR <模板目录>
  OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated   # 可选,默认 <build>/generated
  PROJECT_ROOT <目录>                        # 可选,parse_headers 附加扫描根
  CONFIG <工程无关配置.toml>                  # 可选,gen_path_style/parse_headers/header_markers + CodegenExtension.md §2 的 codegen 注册键
  EXTRA_INCLUDE_PATHS <目录>...)              # 可选,追加 include 路径
```

注意:多目标时给每个目标单独的 `OUTPUT_DIR`(每次生成按目标重写 `all_include.gen.h`);INTERFACE/umbrella 目标在 compile db 中无条目,不支持。

## 8. 已知限制(未实现/有缺陷,改造前勿依赖)

1. **成员方法序列化模板有误**:`Serialization.mustache` 对 `function_list` 生成 `node["x"] = v.x;`(取函数指针,无法编译),decode 完全忽略方法。
2. **枚举底层类型硬编码 `uint32_t`**,且枚举常量名不解析;`ENUM_CLASS` 宏丢弃指定的底层类型。
3. **类型分类仍薄弱**:无类型分类(map/optional/智能指针/数组未处理);字段类型全限定重写是注册表字符串匹配(见 §3),跨 TU 前向声明后才定义的类型、匿名命名空间等边角场景可能解析不到而原样保留;不支持的类型会原样进模板,依赖 yaml-cpp 内建转换,失败时生成代码编译不过且生成器无校验。
4. **属性只有无值标签**,不支持 `rename/skip/default` 等 key=value。
5. 模板类(`CXCursor_ClassTemplate`)、函数内/`extern "C"` 内的类型不收集。
6. `EditorUi.mustache` 无条件写 `v.is_dirty = true`(类型需有该成员);`ObjectHandleSerialization.mustache` 的 decode 有误。
7. 生成结果不校验合法性(不编译、无 schema 检查)。
8. `parse_headers` 只收集文本含注解标记的头:仅在 .cpp 中注解的类型不会被收集;头文件必须自包含(约定,解析失败会显式报错)。

## 9. 目录速览

```
meta/                    标注宏定义
example/                 解析示例(测试输入);cmake_integration/ 为 CMake 集成示例;
                         custom_templates/ 为免编译自定义模板示例(CodegenExtension.md §2.6)
Template/                mustache 模板
source/                  Parser(libclang 封装+收集)、CodeGenerator、Config、Utils、CMakeAnalyzer(编译数据库分析+头文件扫描+target 级生成)
cmake/                   AternyxMetaParser.cmake(aternyx_target_codegen 集成函数 + aternyx_add_parser_tool 自定义工具构建)
test/                    GTest(与 source/ 结构镜像)
docs/                    本文档(设计与实现) + CodegenExtension.md(自定义 Codegen 指南)
```
