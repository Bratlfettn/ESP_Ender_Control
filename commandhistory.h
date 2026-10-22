#pragma once

#include <Arduino.h>

#include "Config.h"

class CommandHistory
{
public:
    struct Entry
    {
        bool valid;
        bool acknowledged;

        uint32_t lineNumber;
        uint32_t fileStartPosition;
        uint32_t fileEndPosition;

        char command[MAX_GCODE_LINE_LENGTH];
    };

    CommandHistory();

    void clear();

    bool store(
        uint32_t lineNumber,
        const char* command,
        uint32_t fileStartPosition,
        uint32_t fileEndPosition
    );

    bool contains(uint32_t lineNumber) const;

    bool get(
        uint32_t lineNumber,
        Entry& output
    ) const;

    const Entry* find(uint32_t lineNumber) const;

    bool markAcknowledged(uint32_t lineNumber);

    bool isAcknowledged(uint32_t lineNumber) const;

    bool remove(uint32_t lineNumber);

    bool removeOlderThan(uint32_t firstLineToKeep);

    uint32_t oldestLineNumber() const;
    uint32_t newestLineNumber() const;

    size_t count() const;
    size_t capacity() const;
    bool empty() const;
    bool full() const;

private:
    Entry entries_[MAX_HISTORY_LINES];

    size_t nextWriteIndex_;
    size_t entryCount_;

    int findIndex(uint32_t lineNumber) const;

    void clearEntry(size_t index);

    void copyCommand(
        char* destination,
        size_t destinationSize,
        const char* source
    );
};