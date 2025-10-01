# Aternyx C++ Code Style Guide

## 命名规范
- 命名空间：`CamelCase`，如 `Aternyx::Rendering`
- 类/结构体/枚举：`CamelCase`，如 `LogSystem`
- 枚举常量：`CamelCase`，如 `ErrorUnknown`
- 函数名：`CamelCase`，如 `CreateBuffer`
- 变量/参数：`camelBack`，如 `bufferHandle`
- 成员变量：`camelBack` + `_` 后缀，如 `impl_`
- 常量：`k` 前缀 + `CamelCase`，如 `kDefaultSize`
- 局部临时变量允许短名（如 `i`, `j`），其他变量需有语义

## 代码风格
- 缩进统一使用 clang-format 配置（推荐 2 或 4 空格）
- 所有控制语句必须加花括号 `{}`
- 文件头部加 `#pragma once`
- 头文件只声明接口，cpp 文件实现细节
- 禁止裸指针，优先智能指针
- 禁止异常和 RTTI
- 构造/析构/赋值运算符需显式声明/删除
- 允许使用现代 C++ 特性

## 设计模式
- 单例模式采用静态局部变量实现，禁止多线程不安全写法, 示例如下:
    ```c++
    LogSystem& LogSystem::Instance() {
        static LogSystem instance;
        return instance;
    }
    ```
- PImpl 模式统一用 `std::unique_ptr<Impl> impl_`，Impl 只在 cpp 文件定义, 示例如下:
    ```c++
    class Example {
    public:
        Example();
        ~Example();
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
    ```

## 其他
- 禁止魔法数字，所有常量需有命名
- 允许使用范围 for、auto、constexpr、智能指针等现代 C++ 特性
- 代码需通过 clang-tidy 检查，无警告
- 命名规则与 .clang-tidy 保持一致
