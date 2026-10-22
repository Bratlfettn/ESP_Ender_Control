#include "MarlinProtocol.h"

#include "Config.h"
#include "Logger.h"

#include <cstdio>
#include <cstring>

MarlinProtocol::MarlinProtocol()
    : nextLineNumber_(1)
{
}

void MarlinProtocol::reset(uint32_t firstLineNumber)
{
    nextLineNumber_ = firstLineNumber;

#if DEBUG_PROTOCOL
    Logger::infof(
        "Marlin protocol line counter reset to %lu",
        static_cast<unsigned long>(nextLineNumber_)
    );
#endif
}

uint32_t MarlinProtocol::nextLineNumber() const
{
    return nextLineNumber_;
}

bool MarlinProtocol::buildCommand(
    const char* rawCommand,
    char* output,
    size_t outputSize,
    uint32_t& assignedLineNumber
)
{
    assignedLineNumber = 0;

    if (rawCommand == nullptr)
    {
        Logger::error(
            "Cannot build Marlin command from null input"
        );

        return false;
    }

    if (output == nullptr)
    {
        Logger::error(
            "Cannot build Marlin command into null output"
        );

        return false;
    }

    if (outputSize < 2)
    {
        Logger::error(
            "Marlin command output buffer is too small"
        );

        return false;
    }

    output[0] = '\0';

    char normalizedCommand[MAX_GCODE_LINE_LENGTH];

    if (!normalizeCommand(
            rawCommand,
            normalizedCommand,
            sizeof(normalizedCommand)
        ))
    {
        return false;
    }

    const uint32_t commandLineNumber =
        nextLineNumber_;

    const int prefixLength = snprintf(
        output,
        outputSize,
        "N%lu %s",
        static_cast<unsigned long>(commandLineNumber),
        normalizedCommand
    );

    if (prefixLength < 0)
    {
        output[0] = '\0';

        Logger::error(
            "Failed to format Marlin command"
        );

        return false;
    }

    const size_t prefixLengthValue =
        static_cast<size_t>(prefixLength);

    if (prefixLengthValue >= outputSize)
    {
        output[0] = '\0';

        Logger::error(
            "Marlin command output buffer is too small for numbered command"
        );

        return false;
    }

    const uint8_t checksum =
        calculateChecksum(output);

    const size_t remainingOutputSize =
        outputSize - prefixLengthValue;

    const int checksumLength = snprintf(
        output + prefixLengthValue,
        remainingOutputSize,
        "%c%u",
        static_cast<char>(42),
        static_cast<unsigned int>(checksum)
    );

    if (checksumLength < 0)
    {
        output[0] = '\0';

        Logger::error(
            "Failed to append Marlin checksum"
        );

        return false;
    }

    if (
        static_cast<size_t>(checksumLength) >=
        remainingOutputSize
    )
    {
        output[0] = '\0';

        Logger::error(
            "Marlin command output buffer is too small for checksum"
        );

        return false;
    }

    assignedLineNumber = commandLineNumber;
    ++nextLineNumber_;

#if DEBUG_PROTOCOL
    Logger::infof(
        "Built Marlin command for line %lu",
        static_cast<unsigned long>(assignedLineNumber)
    );
#endif

    return true;
}

bool MarlinProtocol::buildLineNumberReset(
    char* output,
    size_t outputSize
)
{
    if (output == nullptr)
    {
        Logger::error(
            "Cannot build line-number reset into null output"
        );

        return false;
    }

    const char resetCommand[] = "M110 N0";

    const size_t requiredSize =
        sizeof(resetCommand);

    if (outputSize < requiredSize)
    {
        if (outputSize > 0)
        {
            output[0] = '\0';
        }

        Logger::error(
            "Output buffer is too small for line-number reset command"
        );

        return false;
    }

    memcpy(
        output,
        resetCommand,
        requiredSize
    );

    nextLineNumber_ = 1;

#if DEBUG_PROTOCOL
    Logger::info(
        "Built Marlin line-number reset command"
    );
#endif

    return true;
}

uint8_t MarlinProtocol::calculateChecksum(
    const char* text
) const
{
    if (text == nullptr)
    {
        return 0;
    }

    uint8_t checksum = 0;

    while (*text != '\0')
    {
        checksum ^=
            static_cast<uint8_t>(*text);

        ++text;
    }

    return checksum;
}

bool MarlinProtocol::normalizeCommand(
    const char* input,
    char* output,
    size_t outputSize
) const
{
    if (input == nullptr)
    {
        return false;
    }

    if (output == nullptr || outputSize < 2)
    {
        return false;
    }

    output[0] = '\0';

    const char* commandStart = input;

    while (
        *commandStart == ' ' ||
        *commandStart == '\t'
    )
    {
        ++commandStart;
    }

    size_t commandLength =
        strlen(commandStart);

    while (commandLength > 0)
    {
        const char finalCharacter =
            commandStart[commandLength - 1];

        if (
            finalCharacter == ' ' ||
            finalCharacter == '\t' ||
            finalCharacter == '\r' ||
            finalCharacter == '\n'
        )
        {
            --commandLength;
            continue;
        }

        break;
    }

    if (commandLength == 0)
    {
        Logger::warning(
            "Cannot build an empty Marlin command"
        );

        return false;
    }

    if ((commandLength + 1) > outputSize)
    {
        Logger::errorf(
            "Raw G-code command is too long: %u bytes",
            static_cast<unsigned int>(commandLength)
        );

        return false;
    }

    memcpy(
        output,
        commandStart,
        commandLength
    );

    output[commandLength] = '\0';

    return true;
}