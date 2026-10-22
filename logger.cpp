#include "Logger.h"

#include <stdarg.h>

void Logger::begin(uint32_t baudRate)
{
    static_cast<void>(baudRate);
}

void Logger::info(const char* message)
{
    static_cast<void>(message);
}

void Logger::warning(const char* message)
{
    static_cast<void>(message);
}

void Logger::error(const char* message)
{
    static_cast<void>(message);
}

void Logger::infof(const char* format, ...)
{
    static_cast<void>(format);
}

void Logger::warningf(const char* format, ...)
{
    static_cast<void>(format);
}

void Logger::errorf(const char* format, ...)
{
    static_cast<void>(format);
}

void Logger::tx(const char* line)
{
    static_cast<void>(line);
}

void Logger::rx(const char* line)
{
    static_cast<void>(line);
}

void Logger::tx(const String& line)
{
    static_cast<void>(line);
}

void Logger::rx(const String& line)
{
    static_cast<void>(line);
}

void Logger::printPrefix(const char* level)
{
    static_cast<void>(level);
}

void Logger::printFormatted(
    const char* level,
    const char* format,
    va_list arguments
)
{
    static_cast<void>(level);
    static_cast<void>(format);
    static_cast<void>(arguments);
}