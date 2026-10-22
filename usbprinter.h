#pragma once

#include <Arduino.h>
#include <EspUsbHost.h>

class UsbPrinter
{
public:
    UsbPrinter();

    // Starts the USB host and prepares the CDC serial interface.
    bool begin(uint32_t baudRate);

    // Call frequently from loop().
    // Currently handles connection-state reporting.
    void loop();

    // Returns true when a USB CDC serial device is ready.
    bool connected() const;

    // Returns the number of bytes currently waiting in the RX buffer.
    int available();

    // Reads one raw byte.
    // Returns -1 when no byte is available.
    int read();

    // Reads one complete printer response line.
    //
    // Returns true only when a complete line ending in '\n' or '\r'
    // has been received.
    //
    // Empty lines are discarded.
    //
    // If a line exceeds outputSize - 1, the remainder of that line
    // is discarded and false is returned.
    bool readLine(char* output, size_t outputSize);

    // Sends a null-terminated G-code command.
    // CR/LF characters already present at the end are removed.
    // Exactly one '\n' is appended before transmission.
    bool writeLine(const char* line);

    // Convenience overload.
    bool writeLine(const String& line);

    // Waits until the bounded USB transmit queue is empty.
    //
    // This must not be used after every G-code command.
    // It is intended only for shutdown and diagnostic operations.
    bool flush(uint32_t timeoutMs);

    // Clears a partially received printer response.
    void clearReceiveLine();

    uint32_t baudRate() const;

private:
    static constexpr size_t RX_BUFFER_SIZE = 4096;

    // Four reusable USB OUT transfers are enough for the later
    // Marlin command window without allowing unbounded allocation.
    static constexpr size_t TX_QUEUE_DEPTH = 4;

    // A numbered G-code line will remain well below this size.
    static constexpr size_t TX_QUEUE_BUFFER_SIZE = 256;

    static constexpr size_t INTERNAL_RX_LINE_SIZE = 512;

    EspUsbHost usbHost_;
    EspUsbHostCdcSerial serial_;

    uint32_t baudRate_;

    bool started_;
    bool previousConnectionState_;
    bool receiveLineOverflow_;

    char receiveLine_[INTERNAL_RX_LINE_SIZE];
    size_t receiveLineLength_;

    bool startWriteQueue();
    void updateConnectionState();
};
