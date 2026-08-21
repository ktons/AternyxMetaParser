---
name: aternyx-meta-parser
description: AternyxMetaParser - C++ metadata reflection/parsing/codegen tool using libclang + Mustache templates. Use when working on the AternyxMetaParser project (git@github.com:ktons/AternyxMetaParser.git), including code generation, template development, build configuration, and code generation validation.
---

# AternyxMetaParser

C++ 元数据解析 + 代码生成工具。使用 libclang 扫描源码中带注解的类/结构体/枚举，通过 Mustache 模板自动生成序列化、编辑器中 ImGui 面板、枚举 YAML 转换等代码。

仓库：`git@github.com:ktons/AternyxMetaParser.git`

## 架构概览

```
source/
├── parser_main.cpp        # 入口
├── parser/                # libclang AST解析
│   ├── parser.h/cpp       # MetaParser - 解析入口，遍历AST
│   └── meta_info.h/cpp    # MetaStruct/MetaField/AstTree - 解析结果数据结构
├── cursor/                # libclang CXCursor/CXType 封装
│   ├── cursor.h/cpp       # Cursor - 游标包装(类型、子节点、源文件)
│   └── cursor_type.h/cpp  # CursorType - 类型信息包装
├── code_generator/        # Mustache模板渲染 + 代码生成
│   └── code_generator.h/cpp
├── config/
│   ├── arg_config.h/cpp   # 命令行参数 + TOML配置解析
│   └── utils.h/cpp        # 字符串工具
Template/                  # Mustache 模板文件
├── EnumCast.mustache      # 枚举 ↔ YAML 转换
├── Serialization.mustache # 结构体序列化
├── EditorUi.mustache      # ImGui编辑面板
├── VisitEditorUi.mustache # variant编辑器访问
├── Variant.mustache       # 多态variant生成
├── ObjectHandleSerialization.mustache # 对象句柄序列化
└── _AllInclude.mustache   # 汇总头文件
meta/                      # 给被解析代码引用的头文件
└── meta_attributes.h      # CLASS/STRUCT/ENUM_CLASS宏定义
example/                   # 示例代码
├── main.cpp
├── user_struct.h          # 带注解的示例结构体
└── params.toml            # 示例配置
3rdparty/                  # 依赖
├── LLVM/                  # llvm/libclang (预编译, Win/Mac)
├── Mustache/              # kainjow/Mustache (header-only)
├── argparse/              # (header-only)
└── tomlplusplus/          # (header-only)
```

## 注解机制

通过 `meta/meta_attributes.h` 定义宏，在 libclang 解析时展开为 `__attribute__((annotate(...)))`：

```cpp
// 被解析的源码中：
CLASS(MyClass, Serialization, EditorUi) { ... };
STRUCT(Data, EditorUi) { ... };

// libclang 解析时实际看到的是 annotated attribute，
// code_generator 根据 annotate 字符串中的标签来决定生成什么代码。
```

现有支持的标签：
- `Serialization` → 生成 YAML 序列化代码
- `EditorUi` → 生成 ImGui 编辑面板代码
- `CustomUi` → VisitEditorUi 中走自定义 UI 路径
- `EnumCast` → 生成 YAML 枚举转换
- `Runtime` → 跳过该字段（不生成代码）

## 解析流程

```
源码 → MetaParser::BuildCursor()
  └→ clang_createIndex() + clang_createTranslationUnitFromSourceFile()
    └→ 遍历 AST (buildMode状态机：0=搜索结构体, 1=读取注解, 2=读取字段/基类)
      └→ MetaStruct 推送至 AstTree
        └→ CodeGenerator 按 sourceFilePath 分组
          └→ Mustache 渲染 → _Generated/ 目录输出
```

编译参数中自动注入：
- `-D__REFLECTION_PARSER__` — 使 meta_attributes.h 展开 annotate
- `-std=c++11` — libclang 解析模式
- `-MG -M` — 依赖文件扫描

## 构建

### Windows (推荐)
```bash
# 安装 LLVM (获取 libclang.lib/libclang.dll)，确保在 PATH
# Developer PowerShell for VS 中
cmake --preset ninja-debug-msvc
cmake --build --preset ninja-debug-msvc --target AternyxParser
```

### macOS
libclang 在 `3rdparty/LLVM/bin/macOS/`，已有预编译支持。

### Linux (当前环境)
`3rdparty/CMakeLists.txt` 无 Linux 分支。要适配：
1. `apt install libclang-dev` 获取头文件
2. 修改 `3rdparty/CMakeLists.txt` 添加 Linux 分支：
   ```cmake
   elseif(UNIX)
     find_package(LLVM REQUIRED CONFIG)
     set_target_properties(libclang PROPERTIES
       IMPORTED_LOCATION "${LLVM_LIBRARY_DIR}/libclang.so"
       INTERFACE_INCLUDE_DIRECTORIES "${LLVM_INCLUDE_DIRS}"
     )
   ```
3. 或直接指向系统 libclang 路径

## 运行

```bash
# 基本用法
AternyxParser source/main.cpp -i include/path -i another/path

# 指定模板目录和输出目录
AternyxParser source/main.cpp \
  -t ./Template \
  -o ./_Generated \
  -p ./project_root \
  -i include/path

# 使用TOML配置
AternyxParser source/main.cpp --toml params.toml
```

## 代码风格

遵循 `STYLE.md` 和 `.clang-format` 配置。关键规则：
- 命名空间：`CamelCase`
- 类/结构体：`CamelCase`
- 函数/变量：`camelBack`，成员变量加 `_` 后缀
- 常量：`k` 前缀 + `CamelCase`
- 单例：静态局部变量
- PImpl：`std::unique_ptr<Impl> impl_`

## Skill 相关路径

项目路径：`/root/.openclaw/workspace/AternyxMetaParser/`
