#pragma once

#include <Arduino.h>

class Logger
{
public:
    static void begin(uint32_t baudRate);

    static void info(const char* message);
    static void warning(const char* message);
    static void error(const char* message);

    static void infof(const char* format, ...);
    static void warningf(const char* format, ...);
    static void errorf(const char* format, ...);

    static void tx(const char* line);
    static void rx(const char* line);

    static void tx(const String& line);
    static void rx(const String& line);

private:
    static void printPrefix(const char* level);
    static void printFormatted(
        const char* level,
        const char* format,
        va_list arguments
    );
};