#include "CommandHistory.h"

#include "Logger.h"

#include <cstring>

CommandHistory::CommandHistory()
    : entries_{},
      nextWriteIndex_(0),
      entryCount_(0)
{
    clear();
}

void CommandHistory::clear()
{
    for (size_t index = 0; index < MAX_HISTORY_LINES; ++index)
    {
        clearEntry(index);
    }

    nextWriteIndex_ = 0;
    entryCount_ = 0;
}

bool CommandHistory::store(
    uint32_t lineNumber,
    const char* command,
    uint32_t fileStartPosition,
    uint32_t fileEndPosition
)
{
    if (lineNumber == 0)
    {
        Logger::error(
            "Cannot store command history entry with line number zero"
        );

        return false;
    }

    if (command == nullptr)
    {
        Logger::error(
            "Cannot store null command in command history"
        );

        return false;
    }

    const size_t commandLength = strlen(command);

    if (commandLength == 0)
    {
        Logger::error(
            "Cannot store empty command in command history"
        );

        return false;
    }

    if (commandLength >= MAX_GCODE_LINE_LENGTH)
    {
        Logger::errorf(
            "Command history entry is too long: %u bytes",
            static_cast<unsigned int>(commandLength)
        );

        return false;
    }

    if (fileEndPosition < fileStartPosition)
    {
        Logger::errorf(
            "Invalid file positions for protocol line %lu",
            static_cast<unsigned long>(lineNumber)
        );

        return false;
    }

    const int existingIndex = findIndex(lineNumber);

    if (existingIndex >= 0)
    {
        Entry& existingEntry =
            entries_[static_cast<size_t>(existingIndex)];

        existingEntry.valid = true;
        existingEntry.acknowledged = false;
        existingEntry.lineNumber = lineNumber;
        existingEntry.fileStartPosition = fileStartPosition;
        existingEntry.fileEndPosition = fileEndPosition;

        copyCommand(
            existingEntry.command,
            sizeof(existingEntry.command),
            command
        );

        return true;
    }

    Entry& destination = entries_[nextWriteIndex_];

    if (destination.valid)
    {
#if DEBUG_PROTOCOL
        Logger::warningf(
            "Command history overwriting protocol line %lu",
            static_cast<unsigned long>(destination.lineNumber)
        );
#endif
    }
    else
    {
        ++entryCount_;
    }

    destination.valid = true;
    destination.acknowledged = false;
    destination.lineNumber = lineNumber;
    destination.fileStartPosition = fileStartPosition;
    destination.fileEndPosition = fileEndPosition;

    copyCommand(
        destination.command,
        sizeof(destination.command),
        command
    );

    ++nextWriteIndex_;

    if (nextWriteIndex_ >= MAX_HISTORY_LINES)
    {
        nextWriteIndex_ = 0;
    }

    return true;
}

bool CommandHistory::contains(uint32_t lineNumber) const
{
    return findIndex(lineNumber) >= 0;
}

bool CommandHistory::get(
    uint32_t lineNumber,
    Entry& output
) const
{
    const int index = findIndex(lineNumber);

    if (index < 0)
    {
        return false;
    }

    output = entries_[static_cast<size_t>(index)];

    return true;
}

const CommandHistory::Entry* CommandHistory::find(
    uint32_t lineNumber
) const
{
    const int index = findIndex(lineNumber);

    if (index < 0)
    {
        return nullptr;
    }

    return &entries_[static_cast<size_t>(index)];
}

bool CommandHistory::markAcknowledged(
    uint32_t lineNumber
)
{
    const int index = findIndex(lineNumber);

    if (index < 0)
    {
        return false;
    }

    entries_[static_cast<size_t>(index)].acknowledged = true;

    return true;
}

bool CommandHistory::isAcknowledged(
    uint32_t lineNumber
) const
{
    const int index = findIndex(lineNumber);

    if (index < 0)
    {
        return false;
    }

    return entries_[static_cast<size_t>(index)].acknowledged;
}

bool CommandHistory::remove(uint32_t lineNumber)
{
    const int index = findIndex(lineNumber);

    if (index < 0)
    {
        return false;
    }

    clearEntry(static_cast<size_t>(index));

    if (entryCount_ > 0)
    {
        --entryCount_;
    }

    return true;
}

bool CommandHistory::removeOlderThan(
    uint32_t firstLineToKeep
)
{
    bool removedAtLeastOneEntry = false;

    for (size_t index = 0; index < MAX_HISTORY_LINES; ++index)
    {
        Entry& entry = entries_[index];

        if (!entry.valid)
        {
            continue;
        }

        if (entry.lineNumber >= firstLineToKeep)
        {
            continue;
        }

        clearEntry(index);

        if (entryCount_ > 0)
        {
            --entryCount_;
        }

        removedAtLeastOneEntry = true;
    }

    return removedAtLeastOneEntry;
}

uint32_t CommandHistory::oldestLineNumber() const
{
    uint32_t oldestLine = 0;
    bool foundEntry = false;

    for (size_t index = 0; index < MAX_HISTORY_LINES; ++index)
    {
        const Entry& entry = entries_[index];

        if (!entry.valid)
        {
            continue;
        }

        if (
            !foundEntry ||
            entry.lineNumber < oldestLine
        )
        {
            oldestLine = entry.lineNumber;
            foundEntry = true;
        }
    }

    if (!foundEntry)
    {
        return 0;
    }

    return oldestLine;
}

uint32_t CommandHistory::newestLineNumber() const
{
    uint32_t newestLine = 0;
    bool foundEntry = false;

    for (size_t index = 0; index < MAX_HISTORY_LINES; ++index)
    {
        const Entry& entry = entries_[index];

        if (!entry.valid)
        {
            continue;
        }

        if (
            !foundEntry ||
            entry.lineNumber > newestLine
        )
        {
            newestLine = entry.lineNumber;
            foundEntry = true;
        }
    }

    if (!foundEntry)
    {
        return 0;
    }

    return newestLine;
}

size_t CommandHistory::count() const
{
    return entryCount_;
}

size_t CommandHistory::capacity() const
{
    return MAX_HISTORY_LINES;
}

bool CommandHistory::empty() const
{
    return entryCount_ == 0;
}

bool CommandHistory::full() const
{
    return entryCount_ >= MAX_HISTORY_LINES;
}

int CommandHistory::findIndex(
    uint32_t lineNumber
) const
{
    if (lineNumber == 0)
    {
        return -1;
    }

    for (size_t index = 0; index < MAX_HISTORY_LINES; ++index)
    {
        const Entry& entry = entries_[index];

        if (!entry.valid)
        {
            continue;
        }

        if (entry.lineNumber == lineNumber)
        {
            return static_cast<int>(index);
        }
    }

    return -1;
}

void CommandHistory::clearEntry(size_t index)
{
    if (index >= MAX_HISTORY_LINES)
    {
        return;
    }

    entries_[index].valid = false;
    entries_[index].acknowledged = false;

    entries_[index].lineNumber = 0;
    entries_[index].fileStartPosition = 0;
    entries_[index].fileEndPosition = 0;

    entries_[index].command[0] = '\0';
}

void CommandHistory::copyCommand(
    char* destination,
    size_t destinationSize,
    const char* source
)
{
    if (
        destination == nullptr ||
        destinationSize == 0
    )
    {
        return;
    }

    destination[0] = '\0';

    if (source == nullptr)
    {
        return;
    }

    size_t copyLength = strlen(source);

    if (copyLength >= destinationSize)
    {
        copyLength = destinationSize - 1;
    }

    memcpy(
        destination,
        source,
        copyLength
    );

    destination[copyLength] = '\0';
}