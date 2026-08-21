# AternyxMetaParser 架构详解

## 解析层 (Parser)

### MetaParser (`source/parser/`)
PImpl 模式实现，内部 `Impl` 管理三个组件：
1. **libclang 参数列表** — 包含 `-D__REFLECTION_PARSER__`、`-std=c++11`、`-I` 路径
2. **状态机 (buildMode)** — `0=搜索结构体声明`, `1=读annotate`, `2=读字段/基类`
3. **临时 MetaStruct** — 正在构建中的结构体，遇到新声明时入队

状态流转：
```
buildMode=0 → 发现 CXCursor_StructDecl/ClassDecl/EnumDecl
  → 入队上一个 MetaStruct，创建新的，buildMode=1
buildMode=1 → 遇到 CXCursor_AnnotateAttr → 解析标注 → buildMode=2
            → 没遇到 → buildMode=0 (放弃)
buildMode=2 → 读字段/基类 annotate
            → 发现新的结构体声明 → buildMode=1 (嵌套)
            → 遇到 Namespace 结束 → 入队 → buildMode=0
```

### Cursor (`source/cursor/`)
- `Cursor` — 封装 `CXCursor`，提供 `getChildren()`、`getSpelling()`、`getSourceFile()`、`getType()`
- `CursorType` — 封装 `CXType`，提供 `GetDisplayName()`、`GetCanonicalType()`、`IsConst()`

### AstTree / MetaStruct (`source/parser/meta_info.h`)
- `AstTree` — 全局元数据容器，持有 `vector<MetaStruct>`
  - `typeNameSet_` — 已注册的完整类型名（含命名空间）
  - `metaStructMap_` — typeName → index 映射
  - `GetTypeName()` — 将 libclang 返回的类型名解析为含命名空间的完整名
- `MetaStruct` — 单个被标注的结构体/类
  - `kind` — `CXCursor_StructDecl` / `CXCursor_ClassDecl` / `CXCursor_EnumDecl`
  - `fields` — 成员字段列表（`MetaField`）
  - `attributes` — annotate 拆分后的标签数组（`Split(attributeStr, ",")`）
  - `derivedTypeIndex` — 继承自它的子类在 `metaStructList` 中的索引
- `MetaField` — 单个字段
  - `metaFieldType` — `Property` 或 `Function`
  - `attributes` — 该字段的 annotate 标签

## 生成层 (Code Generator)

### CodeGenerator (`source/code_generator/`)
1. `Init()` — 加载 Template/ 下所有 `.mustache` 文件
2. `SetAstTree()` — 接收解析结果，按 `sourceFilePath` 分组
3. `Run()` — 三段式生成：
   - **Type A** (无依赖, 如 EnumCast) — 全量匹配后一次性生成
   - **Type B/C** (按源文件分组, 如 Serialization) — 每个源文件单独生成
   - **Type E** (_AllInclude) — 汇总所有生成的头文件

### 模板配置表
```cpp
{"EnumCast",                TempType::SERIALIZATION,  TYPE_A, "enum_cast.gen.h"},
{"Serialization",           TempType::SERIALIZATION,  TYPE_B, ".gen.h"},
{"ObjectHandleSerialization", TempType::SERIALIZATION, TYPE_B, "_object_handle.gen.h"},
{"EditorUi",                TempType::EDITOR_UI,     TYPE_B, ".gen.h"},
{"VisitEditorUi",           TempType::EDITOR_UI,     TYPE_C, "_visit_ui.gen.h"},
{"Variant",                 TempType::REFLECTION,    TYPE_B, "_variant.gen.h"},
{"_AllInclude",             TempType::NONE,          TYPE_E, "all_include.gen.h"},
```

模板名称与 annotate 标签对应。例如标注 `Serialization` 的结构体，`CodeGenerator` 会用 `Serialization.mustache` 渲染。

### 输出目录结构
```
_Generated/
├── serialization/          # TempType::SERIALIZATION
│   ├── {source}_serialization.gen.h
│   └── {source}_object_handle.gen.h
├── editor_ui/              # TempType::EDITOR_UI
│   ├── {source}_editor_ui.gen.h
│   └── {source}_visit_ui.gen.h
└── reflection/             # TempType::REFLECTION
    └── {source}_variant.gen.h
```

## 命令行参数

```
positional:
  source_file        需要解析的主源文件 (必需)

optional:
  -o, --output-path  输出目录 (默认: _generated)
  -p, --project-path 项目根路径 (用于计算相对包含路径)
  -t, --template-path 模板目录 (默认: template)
  -i, --include-path 包含目录 (可多次指定)
  --toml             TOML配置文件路径
```

TOML 配置覆盖命令行参数：
```toml
[parserParams]
output_path = "_generated"
project_path = ""
template_path = "Template"
include_paths = ["path1", "path2"]
```

## 依赖

| 依赖 | 类型 | 用途 |
|------|------|------|
| LLVM libclang | 动态库 | C++ AST 解析 |
| kainjow/Mustache | header-only | 模板渲染引擎 |
| argparse | header-only | 命令行参数解析 |
| tomlplusplus | header-only | TOML 配置解析 |
