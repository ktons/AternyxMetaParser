#pragma once

#if defined(__REFLECTION_PARSER__)
#define META(...) __attribute__((annotate(#__VA_ARGS__)))
#define CLASS(class_name, ...) class __attribute__((annotate(#__VA_ARGS__))) class_name
#define STRUCT(struct_name, ...) struct __attribute__((annotate(#__VA_ARGS__))) struct_name
#define ENUM_CLASS(enum_name, ...) enum class __attribute__((annotate("EnumCast"))) enum_name
#else
#define META(...)
#define CLASS(class_name, ...) class class_name
#define STRUCT(struct_name, ...) struct struct_name
#define ENUM_CLASS(enum_name, ...) enum class enum_name
#endif  // __REFLECTION_PARSER__