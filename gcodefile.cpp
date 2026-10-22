#include "GcodeFile.h"

#include "Config.h"
#include "Logger.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

GcodeFile::GcodeFile()
    : file_(),
      path_{0},
      fileSize_(0),
      currentLineStartPosition_(0),
      currentLineEndPosition_(0),
      physicalLineNumber_(0),
      endOfFile_(false),
      error_(false)
{
}

bool GcodeFile::open(
    fs::FS& fileSystem,
    const char* path
)
{
    close();

    if (path == nullptr)
    {
        Logger::error("Cannot open G-code file with null path");
        error_ = true;
        return false;
    }

    const size_t pathLength = strlen(path);

    if (pathLength == 0)
    {
        Logger::error("Cannot open G-code file with empty path");
        error_ = true;
        return false;
    }

    if (pathLength >= PATH_BUFFER_SIZE)
    {
        Logger::errorf(
            "G-code file path is too long: %u bytes",
            static_cast<unsigned int>(pathLength)
        );

        error_ = true;
        return false;
    }

    file_ = fileSystem.open(path, FILE_READ);

    if (!file_)
    {
        Logger::errorf(
            "Failed to open G-code file: %s",
            path
        );

        error_ = true;
        return false;
    }

    if (file_.isDirectory())
    {
        Logger::errorf(
            "G-code path is a directory: %s",
            path
        );

        file_.close();
        error_ = true;
        return false;
    }

    memcpy(
        path_,
        path,
        pathLength + 1
    );

    fileSize_ = static_cast<uint32_t>(file_.size());
    currentLineStartPosition_ = 0;
    currentLineEndPosition_ = 0;
    physicalLineNumber_ = 0;
    endOfFile_ = fileSize_ == 0;
    error_ = false;

#if DEBUG_SD
    Logger::infof(
        "Opened G-code file: %s",
        path_
    );

    Logger::infof(
        "G-code file size: %lu bytes",
        static_cast<unsigned long>(fileSize_)
    );
#endif

    return true;
}

void GcodeFile::close()
{
    if (file_)
    {
        file_.close();
    }

    path_[0] = '\0';

    fileSize_ = 0;
    currentLineStartPosition_ = 0;
    currentLineEndPosition_ = 0;
    physicalLineNumber_ = 0;
    endOfFile_ = false;
    error_ = false;
}

bool GcodeFile::isOpen() const
{
    return static_cast<bool>(file_);
}

bool GcodeFile::eof() const
{
    return endOfFile_;
}

bool GcodeFile::hasError() const
{
    return error_;
}

const char* GcodeFile::path() const
{
    return path_;
}

uint32_t GcodeFile::size() const
{
    return fileSize_;
}

uint32_t GcodeFile::position() const
{
    if (!file_)
    {
        return 0;
    }

    return static_cast<uint32_t>(file_.position());
}

uint32_t GcodeFile::currentLineStartPosition() const
{
    return currentLineStartPosition_;
}

uint32_t GcodeFile::currentLineEndPosition() const
{
    return currentLineEndPosition_;
}

uint32_t GcodeFile::physicalLineNumber() const
{
    return physicalLineNumber_;
}

float GcodeFile::progressPercent() const
{
    if (!file_ || fileSize_ == 0)
    {
        return 0.0f;
    }

    const uint32_t currentPosition =
        static_cast<uint32_t>(file_.position());

    float progress =
        100.0f *
        static_cast<float>(currentPosition) /
        static_cast<float>(fileSize_);

    if (progress < 0.0f)
    {
        progress = 0.0f;
    }

    if (progress > 100.0f)
    {
        progress = 100.0f;
    }

    return progress;
}

bool GcodeFile::rewind()
{
    return seek(0);
}

bool GcodeFile::seek(uint32_t bytePosition)
{
    if (!file_)
    {
        Logger::error(
            "Cannot seek because no G-code file is open"
        );

        error_ = true;
        return false;
    }

    if (bytePosition > fileSize_)
    {
        Logger::errorf(
            "Cannot seek to byte %lu in a %lu-byte file",
            static_cast<unsigned long>(bytePosition),
            static_cast<unsigned long>(fileSize_)
        );

        error_ = true;
        return false;
    }

    if (!file_.seek(bytePosition))
    {
        Logger::errorf(
            "Failed to seek G-code file to byte %lu",
            static_cast<unsigned long>(bytePosition)
        );

        error_ = true;
        return false;
    }

    currentLineStartPosition_ = bytePosition;
    currentLineEndPosition_ = bytePosition;
    endOfFile_ = bytePosition >= fileSize_;
    error_ = false;

    if (bytePosition == 0)
    {
        physicalLineNumber_ = 0;
    }

#if DEBUG_SD
    Logger::infof(
        "G-code file positioned at byte %lu",
        static_cast<unsigned long>(bytePosition)
    );
#endif

    return true;
}

bool GcodeFile::nextLine(
    char* output,
    size_t outputSize
)
{
    if (output == nullptr)
    {
        Logger::error(
            "Cannot read G-code into null output buffer"
        );

        error_ = true;
        return false;
    }

    if (outputSize < 2)
    {
        Logger::error(
            "G-code output buffer is too small"
        );

        error_ = true;
        return false;
    }

    output[0] = '\0';

    if (!file_)
    {
        Logger::error(
            "Cannot read because no G-code file is open"
        );

        error_ = true;
        return false;
    }

    if (error_ || endOfFile_)
    {
        return false;
    }

    while (!endOfFile_)
    {
        uint32_t lineStartPosition = 0;
        uint32_t lineEndPosition = 0;

        const bool lineRead = readPhysicalLine(
            output,
            outputSize,
            lineStartPosition,
            lineEndPosition
        );

        if (!lineRead)
        {
            output[0] = '\0';
            return false;
        }

        currentLineStartPosition_ = lineStartPosition;
        currentLineEndPosition_ = lineEndPosition;

        if (!prepareCommand(output, outputSize))
        {
            if (error_)
            {
                output[0] = '\0';
                return false;
            }

            output[0] = '\0';
            continue;
        }

        return true;
    }

    output[0] = '\0';
    return false;
}

bool GcodeFile::readPhysicalLine(
    char* output,
    size_t outputSize,
    uint32_t& lineStartPosition,
    uint32_t& lineEndPosition
)
{
    if (output == nullptr)
    {
        Logger::error(
            "Cannot read G-code into a null line buffer"
        );

        error_ = true;
        return false;
    }

    if (outputSize < 2)
    {
        Logger::error(
            "G-code line buffer is too small"
        );

        error_ = true;
        return false;
    }

    output[0] = '\0';

    lineStartPosition =
        static_cast<uint32_t>(
            file_.position()
        );

    lineEndPosition =
        lineStartPosition;

    size_t outputLength = 0;

    bool receivedAnyByte = false;
    bool commentStarted = false;
    bool commandTooLong = false;

    while (file_.available())
    {
        const int value =
            file_.read();

        if (value < 0)
        {
            Logger::error(
                "Failed while reading G-code file"
            );

            error_ = true;
            output[0] = '\0';

            return false;
        }

        receivedAnyByte = true;

        const char character =
            static_cast<char>(value);

        if (character == '\n')
        {
            break;
        }

        if (character == '\r')
        {
            continue;
        }

        if (character == '\0')
        {
            continue;
        }

        if (commentStarted)
        {
            continue;
        }

        if (character == ';')
        {
            commentStarted = true;
            continue;
        }

        if (commandTooLong)
        {
            continue;
        }

        if (
            outputLength >=
            outputSize - 1
        )
        {
            commandTooLong = true;
            continue;
        }

        output[outputLength] =
            character;

        ++outputLength;
    }

    lineEndPosition =
        static_cast<uint32_t>(
            file_.position()
        );

    if (!file_.available())
    {
        endOfFile_ = true;
    }

    if (!receivedAnyByte)
    {
        endOfFile_ = true;
        output[0] = '\0';

        return false;
    }

    ++physicalLineNumber_;

    if (commandTooLong)
    {
        output[0] = '\0';

        Logger::errorf(
            "Executable G-code line %lu exceeds command buffer size",
            static_cast<unsigned long>(
                physicalLineNumber_
            )
        );

        error_ = true;
        return false;
    }

    output[outputLength] = '\0';

    return true;
}

bool GcodeFile::prepareCommand(
    char* line,
    size_t bufferSize
)
{
    if (line == nullptr || bufferSize < 2)
    {
        error_ = true;
        return false;
    }

    removeUtf8Bom(line);
    removeSemicolonComment(line);
    trimWhitespace(line);

    if (line[0] == '\0')
    {
        return false;
    }

    if (
        line[0] == '%' &&
        line[1] == '\0'
    )
    {
        return false;
    }

    removeLineNumberAndChecksum(line);
    trimWhitespace(line);

    if (line[0] == '\0')
    {
        return false;
    }

    if (strlen(line) >= bufferSize)
    {
        Logger::error(
            "Prepared G-code command exceeds output buffer"
        );

        error_ = true;
        return false;
    }

    return true;
}

void GcodeFile::removeUtf8Bom(char* line)
{
    if (line == nullptr)
    {
        return;
    }

    const unsigned char first =
        static_cast<unsigned char>(line[0]);

    const unsigned char second =
        static_cast<unsigned char>(line[1]);

    const unsigned char third =
        static_cast<unsigned char>(line[2]);

    if (
        first == 0xEF &&
        second == 0xBB &&
        third == 0xBF
    )
    {
        const size_t remainingLength =
            strlen(line + 3);

        memmove(
            line,
            line + 3,
            remainingLength + 1
        );
    }
}

void GcodeFile::removeSemicolonComment(char* line)
{
    if (line == nullptr)
    {
        return;
    }

    char* commentStart = strchr(line, ';');

    if (commentStart != nullptr)
    {
        commentStart[0] = '\0';
    }
}

void GcodeFile::trimWhitespace(char* line)
{
    if (line == nullptr)
    {
        return;
    }

    char* firstCharacter = line;

    while (
        firstCharacter[0] != '\0' &&
        isspace(
            static_cast<unsigned char>(
                firstCharacter[0]
            )
        )
    )
    {
        ++firstCharacter;
    }

    if (firstCharacter != line)
    {
        const size_t remainingLength =
            strlen(firstCharacter);

        memmove(
            line,
            firstCharacter,
            remainingLength + 1
        );
    }

    size_t length = strlen(line);

    while (length > 0)
    {
        const unsigned char finalCharacter =
            static_cast<unsigned char>(
                line[length - 1]
            );

        if (!isspace(finalCharacter))
        {
            break;
        }

        line[length - 1] = '\0';
        --length;
    }
}

void GcodeFile::removeLineNumberAndChecksum(
    char* line
)
{
    if (line == nullptr)
    {
        return;
    }

    // Remove an existing checksum from pre-numbered G-code.
    char* checksumSeparator = strchr(
        line,
        static_cast<char>(42)
    );

    if (checksumSeparator != nullptr)
    {
        checksumSeparator[0] = '\0';
        trimWhitespace(line);
    }

    // Keep normal commands beginning with letters other than N.
    if (
        line[0] != 'N' &&
        line[0] != 'n'
    )
    {
        return;
    }

    char* position = line + 1;

    if (!isdigit(
            static_cast<unsigned char>(position[0])
        ))
    {
        return;
    }

    while (
        isdigit(
            static_cast<unsigned char>(position[0])
        )
    )
    {
        ++position;
    }

    if (
        position[0] != ' ' &&
        position[0] != '\t'
    )
    {
        return;
    }

    while (
        position[0] == ' ' ||
        position[0] == '\t'
    )
    {
        ++position;
    }

    const size_t commandLength =
        strlen(position);

    memmove(
        line,
        position,
        commandLength + 1
    );
}