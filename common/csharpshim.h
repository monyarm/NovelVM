#ifndef CSHARPSHIM_H
#define CSHARPSHIM_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/debug.h"

namespace CSharpShim
{

template <typename T>
using List = Common::Array<T>;

template <typename T>
using SortedList = Common::SortedArray<T>;

template <typename T1, typename T2>
using Dictionary = Common::HashMap<T1, T2>;

using string = Common::String;

class Debug
{
public:
    void Print(string s) { debug(s.c_str()); };
    void Print(const char *c) { debug(c); };
};

enum class Type
{
    T_NULL,
    T_byte,
    T_char,
    T_string,
    T_uint16,
    T_int16,
    T_uint32,
    T_int32,
    T_uint64,
    T_int64,

    T_float,

    T_ListByte

};

struct _objectunion {
    byte b;
    char c;
    string s;
    uint16 u16;
    int16 i16;
    uint32 u32;
    int32 i32;
    uint64 u64;
    int64 i64;
    float f;
    List<byte> data;

    ~_objectunion(){
        delete &b;
        delete &c;
        delete &s;
        delete &u16;
        delete &i16;
        delete &u32;
        delete &i32;
        delete &u64;
        delete &i64;
        delete &f;
        delete &data;
    }
};

class objectClass
{
public:
    bool is(Type t) { return type == t; };

    objectClass() { type = Type::T_NULL; };

    objectClass(byte b)
    {
        getValue.b = b;
        type = Type::T_byte;
    };
    objectClass(char c)
    {
        getValue.c = c;
        type = Type::T_char;
    };
    objectClass(string s)
    {
        getValue.s = s;
        type = Type::T_string;
    };
    objectClass(uint16 u16)
    {
        getValue.u16 = u16;
        type = Type::T_uint16;
    };
    objectClass(int16 i16)
    {
        getValue.i16 = i16;
        type = Type::T_int16;
    };
    objectClass(uint32 u32)
    {
        getValue.u32 = u32;
        type = Type::T_uint32;
    };
    objectClass(int32 i32)
    {
        getValue.i32 = i32;
        type = Type::T_int32;
    };
    objectClass(uint64 u64)
    {
        getValue.u64 = u64;
        type = Type::T_uint64;
    };
    objectClass(int64 i64)
    {
        getValue.i64 = i64;
        type = Type::T_int64;
    };
    objectClass(float f)
    {
        getValue.f = f;
        type = Type::T_float;
    };
    objectClass(List<byte> data)
    {
        getValue.data = data;
        type = Type::T_ListByte;
    };

    _objectunion getValue;
    Type type;
};
typedef objectClass object;

} // namespace CSharpShim

#endif