#pragma once
#include "Arduino.h"
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;
};
class Stream : public Print {
public:
    virtual int read() = 0;
    virtual int peek() = 0;
    virtual int available() = 0;
    virtual void flush() = 0;
};
