
# AternyxMetaParser 简易说明 / Quick Guide

## 依赖 Dependencies

- [llvm/llvm-project (libclang)](https://github.com/llvm/llvm-project)
- [kainjow/Mustache](https://github.com/kainjow/Mustache)

## 简介 Introduction

这是一个 C++ 元数据解析和代码生成的初步版本。它可以扫描你的 C++ 源码，结合 `Template/` 下的 mustache 模板，自动生成代码到 `_Generated/` 目录。

---

## 如何使用 How to Use

1. **编译项目 Build the Project**
   - CMakePreset + MSVC
   - 需要安装LLVM以获取libclang.dll, 注意添加到PATH
   - 推荐使用 `Developr PowerShell for VS` 打开目录，然后 `Code .` 用VSCode打开
   - 然后 `CMake --build --preset ninja-debug --target AternyxParser` 编译即可

2. **运行解析 Run the Parser**
   - 命令行示例：
     ```
     .\build\bin\AternyxParser.exe .\Example\main.cpp .\Example\
     ```
   - 第一个参数是主源文件，后面是 include 路径。

3. **查看生成结果 Check Output**
   - 查看 `Template/` 目录，了解有哪些模板可用。
   - 运行后，自动生成的代码会在 `_Generated/` 目录下。

---
