#pragma once

#include <Arduino.h>

class MarlinProtocol
{
public:
    MarlinProtocol();

    // Resets the local line counter.
    // The next generated command uses firstLineNumber.
    void reset(uint32_t firstLineNumber = 1);

    // Returns the line number that will be used next.
    uint32_t nextLineNumber() const;

    // Creates a numbered Marlin command.
    //
    // Example:
    // N1 G28*18
    //
    // The generated command does not contain CR or LF.
    //
    // assignedLineNumber receives the line number used for this command.
    // The internal counter advances only when generation succeeds.
    bool buildCommand(
        const char* rawCommand,
        char* output,
        size_t outputSize,
        uint32_t& assignedLineNumber
    );

    // Creates a command that resets Marlin's expected line number.
    //
    // Example:
    // M110 N0
    //
    // The generated command is intentionally unnumbered.
    // The next normal command will use line number 1.
    bool buildLineNumberReset(
        char* output,
        size_t outputSize
    );

    // Calculates the Marlin XOR checksum for a string.
    uint8_t calculateChecksum(const char* text) const;

private:
    uint32_t nextLineNumber_;

    bool normalizeCommand(
        const char* input,
        char* output,
        size_t outputSize
    ) const;
};