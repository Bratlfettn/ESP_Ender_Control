#pragma once

#include <Arduino.h>
#include <FS.h>

class GcodeFile
{
public:
    GcodeFile();

    bool open(
        fs::FS& fileSystem,
        const char* path
    );

    void close();

    bool isOpen() const;
    bool eof() const;
    bool hasError() const;

    const char* path() const;

    uint32_t size() const;
    uint32_t position() const;
    uint32_t currentLineStartPosition() const;
    uint32_t currentLineEndPosition() const;
    uint32_t physicalLineNumber() const;

    float progressPercent() const;

    bool rewind();

    bool seek(uint32_t bytePosition);

    bool nextLine(
        char* output,
        size_t outputSize
    );

private:
    static constexpr size_t PATH_BUFFER_SIZE = 128;

    File file_;

    char path_[PATH_BUFFER_SIZE];

    uint32_t fileSize_;
    uint32_t currentLineStartPosition_;
    uint32_t currentLineEndPosition_;
    uint32_t physicalLineNumber_;

    bool endOfFile_;
    bool error_;

    bool readPhysicalLine(
        char* output,
        size_t outputSize,
        uint32_t& lineStartPosition,
        uint32_t& lineEndPosition
    );

    bool prepareCommand(
        char* line,
        size_t bufferSize
    );

    void removeUtf8Bom(char* line);
    void removeSemicolonComment(char* line);
    void trimWhitespace(char* line);
    void removeLineNumberAndChecksum(char* line);
};