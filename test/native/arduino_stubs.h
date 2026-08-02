#pragma once

#include <cstdint>
#include <string>

#ifndef HEX
#define HEX 16
#endif

struct SerialStub
{
    template <typename... Args>
    void println(Args...)
    {
    }

    template <typename... Args>
    void print(Args...)
    {
    }
};

inline SerialStub Serial;

using String = std::string;
