# 自定义 Codegen 指南(上层扩展)

> 面向想定制 AternyxMetaParser codegen 的上层项目。库的设计理念与实现细节见
> `MetaParser.md`,本文只讲怎么扩展。所有扩展方式都**不改动 MetaParser 本身**。

## 1. 扩展模型总览

扩展分两层,**注入物不同**:配置文件只能承载数据,函数(编译后的代码)不可能从配置文件里注入——所以"数据型"定制走模板层,"行为型"定制走 C++ 层:

| 需求 | 落在哪 | 代价 |
|---|---|---|
| 新增模板/类别、禁用某模板或 allinclude、改 prelude、改输出文件名 pattern | **模板层**(TOML + mustache,§2) | 零编译 |
| 产物格式化等"外部命令能做"的事 | 模板层的 `post_process`(§2.4) | 零编译 |
| 类型过滤、给模板上下文加计算字段、任务增删改、产物内容改写 | **C++ 层**(`CodegenHooks`,§3) | 自建小工具 |
| 完全自管生成(不用内置渲染管线) | 逃生舱 `ParseTargetAst`(§3.4) | 自建工具 |

"上层重做 execute"的代价是重写 include 拼写、gen→gen 依赖、allinclude 聚合这些已经打磨过的细节,因此内置管线始终是默认推荐,逃生舱只在真正需要时使用。

## 2. 模板层扩展(免编译)

### 2.1 注册表概念

生成管线是一个通用解释器:读注册表 → 按注解名匹配模板 → 渲染 → 按输出名写文件。注册表由四类数据组成,全部可通过 TOML 定制:

| 数据 | 作用 | 内置默认 |
|---|---|---|
| 类别 `codegen_category` | 开放字符串,决定生成子目录(snake/Camel 两套拼写) | serialization / editor_ui / reflection |
| 模板 `codegen_template` | 注解名 + 类别 + 输出方式 + 排序 + 输出文件名 pattern | 6 个(见 `MetaParser.md` §4 表) |
| 聚合器 `codegen_aggregator` | 每类别一条 allinclude,聚合该类别全部产物 | 每类别一条 `_AllInclude` → `all_include.gen.h` |
| prelude `codegen_prelude` | 注入到各类别 allinclude 头部的 include 列表 | 库默认空;CLI 注入引擎头(见 §2.5) |

触发模型(细节见 `MetaParser.md` §4):

- **注解名即模板名**,匹配大小写不敏感:`CLASS(Foo, PhysicsBind)` 渲染 `PhysicsBind.mustache`;匹配不到任何注册模板的注解被忽略——所以自定义模板不需要动解析端。
- `kind = "per_source"`(默认):按源文件分组,输出 `<源文件stem><output>`(如 `user_struct_summary.gen.h`);`kind = "global"`:整个 run 一份,`output` 即完整文件名(如 `enum_cast.gen.h`)。
- `order` 决定同类别内 allinclude 列表的顺序。

### 2.2 TOML 注册键

通过 `--toml`(或 `aternyx_target_codegen(... CONFIG ...)`)传入。**合并语义**:条目在内置默认之上合并——同名覆盖内置,新名追加,`remove = true` 删除内置项;prelude 按类别整体覆盖(显式空数组 = 清空)。

```toml
# 注册/覆盖生成类别(name 同名覆盖内置;snake_dir 缺省用 name,camel_dir 缺省用 snake_dir)
[[codegen_category]]
name = "physics"
snake_dir = "physics"
camel_dir = "Physics"

# 注册/覆盖模板(注解名即模板名;name 同名覆盖内置,可借覆盖改 output/post_process)
[[codegen_template]]
name = "PhysicsBind"
category = "physics"
output = "_bind.gen.h"
kind = "per_source"        # 可选: per_source(默认) | global
order = 100                # 可选,同类别内排序(allinclude 列表顺序)
post_process = "clang-format --style=file"   # 可选,见 §2.4
gen_include_deps = ["Other"]                 # 可选,本模板的 gen_include_list 额外可见的模板产物(见 §2.3)

# 禁用内置模板 / 某类别的 allinclude
[[codegen_template]]
name = "VisitEditorUi"
remove = true

[[codegen_aggregator]]
category = "physics"       # 自定义类别注册自己的 allinclude 聚合器
template = "_AllInclude"
output = "all_include.gen.h"

[[codegen_aggregator]]
category = "editor_ui"
remove = true              # 禁用 editor_ui 的 allinclude

# 覆盖某类别 allinclude 头部的 prelude(按类别整体覆盖;显式空数组 = 清空)
[codegen_prelude.serialization]
includes = ["<yaml-cpp/yaml.h>"]
```

校验为 fail-fast:模板引用未注册类别、注册表重名、TOML 键值非法都会让生成以非 0 退出码终止。

### 2.3 模板怎么写

模板文件放在 `-t` 指定的模板目录,文件名 = 注册名 + `.mustache`。内置模板(`Template/*.mustache`)是最好的参考;本节列全上下文变量与约定。

**每个类型的上下文**(`meta_type_list` 的条目):

| 变量 | 含义 |
|---|---|
| `is_struct` | 是否 struct(否则 class) |
| `namespace` | 命名空间 |
| `type_name` | 全限定类型名(生成代码里一律用它,字段类型同理已全限定) |
| `simple_type_name` | 简名(可用于标识符拼接) |
| `base_type_name` | 基类全限定名(无基类时不存在;枚举当前写死 `uint32_t`,见 `MetaParser.md` §8——可在 C++ 层用 `decorateTypeData` 修正,§3.1) |
| `child_type_list` | 派生类型列表,条目:`child_is_struct` / `child_namespace` / `child_simple_type_name` / `child_type_name` / `comma`(是否需要逗号) / `custom_ui` / `editor_ui` |
| `property_list` | 属性列表,条目:`name` / `type_name`;`META(Runtime)` 字段被排除 |
| `function_list` | 方法列表(需 `META(Serializable)` 标注),条目:`name` / `type_name` |

**每个任务的上下文**(per_source / global 模板):

| 变量 | 含义 |
|---|---|
| `meta_type_list` | 该任务聚合的全部类型(上表) |
| `include_file_list` | 源头文件 include 拼写,条目 `include_path`,**不带引号** |
| `gen_include_list` | gen→gen 依赖(本文件引用的其他生成物),条目 `gen_include_path`,相对本文件目录、**不带引号**。**按模板亲和过滤(2026-09)**:只含本模板及 `gen_include_deps` 声明模板的产物(同源兄弟 + 传递包含头的产物);互不引用的模板(如同源文件上的两种序列化)产物互相独立,不会互相带入 |

**聚合器模板的上下文**:只有 `include_file_list`,条目 `include_path` = 该类别 prelude(原样)+ 全部产物,**C++ 侧已带引号**。

> **引号约定(最易踩的坑)**:per-file 模板里写 `#include "{{include_path}}"`(模板带引号,与 `gen_include_path` 一致);聚合器模板里写 `#include {{include_path}}`(数据带引号)。内置模板正是这两种写法。

其他约定:

- 渲染结果经 trim,不做 HTML 转义;
- 模板语法错误 = 生成失败(fail-fast);模板文件缺失 = stderr 警告并跳过该条目(可当禁用开关);
- 聚合器约定先 `#include "all_include.gen.h"` 再使用单文件生成头;`__REFLECTION_PARSER__` 守卫写法照抄内置模板(解析模式下生成头不被解析)。

### 2.4 post_process(配置携带行为)

`[[codegen_template]]` 条目上加 `post_process = "<命令>"`:该模板的每个产物经命令 **stdin 进、stdout 出**,`%F` 展开为(shell 引号包裹的)输出文件名;命令失败 = 生成失败。典型用途是产物格式化:

```toml
[[codegen_template]]   # 覆盖内置 Serialization,补一个格式化
name = "Serialization"
category = "serialization"
output = ".gen.h"
post_process = "clang-format --style=file"
```

这是"配置携带行为"的唯一通道(等价于 C++ 层的 `transformOutput`);脚本引擎/dlopen 插件之类方案明确不做,更复杂的行为用 §3 的钩子。

### 2.5 prelude 与行为变更说明(2026-09)

引擎头文件(yaml-cpp/imgui 等)原先写死在库里,现已迁出:库默认 `preludes` 为空,CLI(`Main.cpp`)注入引擎默认值——`aternyx_target_codegen`/exe 路径行为不变。**直接用库的上层**需要自己注入 preludes,否则 allinclude 不再自动包含引擎头(这是刻意的依赖方向修正,详见 `MetaParser.md` §1)。

### 2.6 可运行示例

`example/custom_templates/`:注册自定义类别 `summary` + 自定义模板 `Summary`(触发点见 `example/user_struct.h` 的 `DataBlock`),并逐一禁用内置模板。

```bash
AternyxParser <compile_commands.json 目录> --target <目标名> \
  -o _generated \
  -t example/custom_templates/templates \
  --toml example/custom_templates/custom_codegen.toml
```

## 3. C++ 层扩展

### 3.1 CodegenHooks(行为注入)

```cpp
Aternyx::CodegenHooks hooks;
hooks.acceptType = [](const Aternyx::MetaStruct& t) {
  // 返回 false 的类型不参与任何规划、渲染与聚合(如按注解过滤内部类型)
  return !Aternyx::StringLib::EqualsNoCase(t.simpleTypeName, "InternalOnly");
};
hooks.decorateTypeData = [](const Aternyx::MetaStruct& t, kainjow::mustache::data& d) {
  d.set("base_type_name", ResolveEnumUnderlyingType(t));   // 例:修正枚举底层类型写死 uint32_t 的缺陷
};
hooks.transformJobs = [](std::vector<Aternyx::GenJob>& jobs) {
  // 计划后、产物注册前:增删改任务,含 category / outputName——下游
  // gen_include_list 与 allinclude 全部跟随改写后的值。新增任务必须引用已注册模板。
};
hooks.transformOutput = [](const Aternyx::GenJob& job, std::string content) -> std::optional<std::string> {
  return content + "\n";   // 渲染后、写盘前改写;返回 std::nullopt 跳过写盘
};
```

管线顺序与语义要点:

```
PlanJobs(解析注解→原始任务,outputName 已定)
  → hooks.transformJobs
  → RegisterOutputs(产物注册表 + 聚合任务,从 transform 后的任务派生)
  → 逐任务渲染(每类型上下文先经 decorateTypeData,产物写盘前经 transformOutput)
```

- `acceptType` 在 AST 消费的两个入口(SetAstTree 建上下文、PlanJobs 匹配)一致生效;
- `transformOutput` 对**所有产物**生效,包括聚合器文件;
- 钩子抛异常 = 整次生成失败(fail-fast);
- 钩子签名直接暴露 `kainjow::mustache::data`——该库已 vendored 且头文件已依赖,不做二次封装;状态放 lambda 捕获即可。

### 3.2 自建解析工具

C++ 钩子需要一个可执行入口:用 `cmake/AternyxMetaParser.cmake` 的 helper 链接 `AternyxParserLib`(要求本仓库已在消费方构建里,`add_subdirectory`/FetchContent):

```cmake
include(<AternyxMetaParser仓库>/cmake/AternyxMetaParser.cmake)
aternyx_add_parser_tool(MyParser SRCS tools/my_parser_main.cpp OUTPUT_NAME my-parser)
aternyx_target_codegen(my_target PARSER $<TARGET_FILE:MyParser> TEMPLATE_DIR ...)
```

工具的 main 就是"构造 options + hooks,调一次 RunTargetCodegen":

```cpp
Aternyx::CMake::TargetCodegenOptions options;
options.outputPath = "...";  options.templatePath = "...";
// 可选:preludes / categories / templates / aggregators(空 = 内置默认)
Aternyx::CMake::RunTargetCodegen(target, options, std::move(hooks));
```

`target` 来自 `CMakeTargetAnalyzer::Analyze(db)` + `FindTarget(name)`(同内置 CLI 的流程)。现成范例:`test/CMakeAnalyzer/TargetCodegenTest.cpp`。

### 3.3 库 API 直驱(不经 TargetCodegen)

`CodeGenerator` 是去单例的可编程组件,一切定制都是 `CodegenConfig` 数据:

```cpp
Aternyx::CodegenConfig config;
config.outputPath = "...";  config.templatePath = "...";
config.includeRoots = /* target 的 -I */;
config.sourceIncludes = /* ParseTargetAst 给的 gen→gen 依赖 */;
config.preludes["serialization"] = {"<yaml-cpp/yaml.h>"};      // 引擎头自己注入
config.templates = Aternyx::DefaultTemplates();                // 空 = 内置默认;
config.templates.push_back({...});                             // 非空 = 整体替换,要"默认+追加"就这样混回去

Aternyx::CodeGenerator generator;
generator.Init(config, std::move(hooks));
generator.SetAstTree(&tree);
generator.Run();
```

### 3.4 逃生舱:ParseTargetAst

想完全自管生成的上层只复用解析层,不必重碰 clang 参数、TU 合并、include 拼写:

```cpp
Aternyx::CMake::TargetParseResult parsed = Aternyx::CMake::ParseTargetAst(target, options);
// parsed.tree          — 全 target 合并去重后的 AST
// parsed.sourceIncludes — 每个源文件的传递 include(内置 gen_include_list 的数据来源)
```

之后自行驱动 `CodeGenerator`(§3.3)或干脆自己写产物。范例:`TargetCodegenTest.ParseTargetAstAllowsCustomGeneration`。

## 4. 选型速查

| 想做的事 | 用什么 |
|---|---|
| 加一种生成物(新注解 → 新头文件) | §2.2 注册模板 + §2.3 写 mustache |
| 关掉某模板 / 某类别 allinclude | §2.2 `remove = true` |
| 产物跑 clang-format | §2.4 `post_process`(免编译) |
| 给模板加"算出来的"字段(如真实枚举底层类型) | §3.1 `decorateTypeData` |
| 按注解/名字过滤类型 | §3.1 `acceptType` |
| 改输出布局、增删生成任务 | §3.1 `transformJobs`(免编译的布局改动走 §2.2 类别/输出名) |
| 产物内容改写/条件跳过写盘 | §3.1 `transformOutput` |
| 不用内置管线,完全自己生成 | §3.4 `ParseTargetAst` + 自己驱动 |
