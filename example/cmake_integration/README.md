# CMake 集成示例

演示用 `aternyx_target_codegen()` 把代码生成挂进构建：目标编译前自动跑
AternyxParser，生成目录同时加入该目标的 include 路径——"输出路径"与"include
路径"是一个决定，只在这里做一次。

## 布局

```
cmake_integration/
├── CMakeLists.txt   # aternyx_target_codegen(example_app ... PARSE_HEADERS)
└── src/
    ├── my_types.h   # 自包含、被注解的头文件（.h-as-source 的解析输入）
    └── main.cpp     # 消费方；__has_include 守卫演示首跑安全
```

## 运行

```bash
# 1. 构建 AternyxMetaParser 本体（产物 build/bin/AternyxParser.exe）
cmake --preset ninja-debug-msvc
cmake --build --preset ninja-debug-msvc --target AternyxParser

# 2. 在 MSVC 环境下配置并构建示例（需 Ninja）
cd example/cmake_integration
cmake -B build -G Ninja -DATERNYX_META_PARSER_ROOT=F:/Project/AternyxMetaParser
cmake --build build
```

构建 `example_app` 时会先执行 codegen（stamp 依赖：目标源文件 +
compile_commands.json + 解析器），生成文件出现在 `build/generated/`：

```
build/generated/serialization/player_state.gen.h
build/generated/serialization/all_include.gen.h
```

## 要点

- `PARSE_HEADERS`：只解析注解头文件（文本预筛含 `CLASS(`/`STRUCT(`/`ENUM_CLASS(`
  的 .h），不解析 .cpp——头文件按约定不 include 生成物，因此首跑不会因生成物
  缺失而失败。
- 生成文件对 `my_types.h` 的 include 拼写取自 target 的 include 目录
  （compile_commands.json），在编译该目标时按构造可解析。
- 生成的序列化代码依赖 yaml-cpp 等运行时头（内置 prelude，见
  `docs/MetaParser.md` 的 prelude 配置）。本示例只演示构建接线，不编译生成
  代码；要在真实项目里消费，请把 `main.cpp` 注释中的 `__has_include` 模式与
  运行时依赖一并接入。
- 多目标时请给每个目标单独的 `OUTPUT_DIR`：每次生成会按目标重写
  `all_include.gen.h` 聚合器。
