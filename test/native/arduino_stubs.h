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

inline unsigned long &millisNow()
{
    static unsigned long value = 0;
    return value;
}

inline unsigned long millis()
{
    return millisNow();
}
