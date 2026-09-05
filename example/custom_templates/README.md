# 自定义模板示例（免编译扩展）

本目录演示**不改 C++** 扩展 codegen：注册自定义模板/类别、给自定义类别配
allinclude 聚合器、禁用内置模板，全部通过 `custom_codegen.toml` 完成。

- `templates/Summary.mustache`：自定义模板，注解名即模板名——类型上标注
  `Summary` 即触发（见 `example/user_struct.h` 的 `DataBlock`）。
- `templates/_AllInclude.mustache`：自定义类别的 allinclude 聚合模板。
- `custom_codegen.toml`：注册表配置（类别、模板、聚合器、remove 禁用项）。

## 运行

```bash
AternyxParser <compile_commands.json 目录> --target <目标名> \
  -o _generated \
  -t example/custom_templates/templates \
  --toml example/custom_templates/custom_codegen.toml
```

生成结果在 `_generated/summary/`：`<源文件名>_summary.gen.h` 汇总该文件中
标注 `Summary` 的类型，`all_include.gen.h` 聚合全部产物。

需要**行为级**定制（类型过滤、给模板上下文加计算字段、任务增删改、产物
改写）时，配置已不够用：链接 `AternyxParserLib` 写钩子即可，见
`docs/CodegenExtension.md` 第 3 节。
