#include "UsbPrinter.h"

#include "Config.h"
#include "Logger.h"

#include <cstring>

UsbPrinter::UsbPrinter()
    : usbHost_(),
      serial_(usbHost_),
      baudRate_(0),
      started_(false),
      previousConnectionState_(false),
      receiveLineOverflow_(false),
      receiveLine_{0},
      receiveLineLength_(0)
{
}

bool UsbPrinter::begin(uint32_t baudRate)
{
    if (started_)
    {
        Logger::warning("UsbPrinter is already started");
        return true;
    }

    baudRate_ = baudRate;

    if (!serial_.setRxBufferSize(RX_BUFFER_SIZE))
    {
        Logger::error("Failed to allocate USB printer RX buffer");
        return false;
    }

    if (!usbHost_.begin())
    {
        Logger::errorf(
            "USB host startup failed: %s",
            usbHost_.lastErrorName()
        );

        return false;
    }

    if (!serial_.begin(baudRate_))
    {
        Logger::error("Failed to start USB CDC serial interface");
        return false;
    }

    clearReceiveLine();

    started_ = true;
    previousConnectionState_ = serial_.connected();

    Logger::infof(
        "USB printer host started at %lu baud",
        static_cast<unsigned long>(baudRate_)
    );

    Logger::infof(
        "USB printer RX buffer size: %u bytes",
        static_cast<unsigned int>(RX_BUFFER_SIZE)
    );

    if (previousConnectionState_)
    {
        Logger::info("USB printer already connected");

        if (!serial_.setBaudRate(baudRate_))
        {
            Logger::warning("Could not configure printer baud rate");
        }

        if (!serial_.setDtr(true))
        {
            Logger::warning("Could not enable printer DTR");
        }

        if (!serial_.setRts(false))
        {
            Logger::warning("Could not disable printer RTS");
        }

        if (!startWriteQueue())
        {
            Logger::error("Could not initialize USB printer TX queue");
            return false;
        }
    }
    else
    {
        Logger::info("Waiting for USB printer connection");
    }

    return true;
}

void UsbPrinter::loop()
{
    if (!started_)
    {
        return;
    }

    updateConnectionState();
}

bool UsbPrinter::connected() const
{
    if (!started_)
    {
        return false;
    }

    return serial_.connected();
}

int UsbPrinter::available()
{
    if (!connected())
    {
        return 0;
    }

    return serial_.available();
}

int UsbPrinter::read()
{
    if (!connected())
    {
        return -1;
    }

    return serial_.read();
}

bool UsbPrinter::readLine(char* output, size_t outputSize)
{
    if (output == nullptr)
    {
        return false;
    }

    if (outputSize < 2)
    {
        return false;
    }

    output[0] = '\0';

    if (!connected())
    {
        return false;
    }

    while (serial_.available() > 0)
    {
        const int receivedValue = serial_.read();

        if (receivedValue < 0)
        {
            break;
        }

        const char receivedCharacter =
            static_cast<char>(receivedValue);

        if (
            receivedCharacter == '\r' ||
            receivedCharacter == '\n'
        )
        {
            if (
                receiveLineLength_ == 0 &&
                !receiveLineOverflow_
            )
            {
                continue;
            }

            if (receiveLineOverflow_)
            {
                Logger::error(
                    "Printer response exceeded internal RX line buffer"
                );

                clearReceiveLine();
                return false;
            }

            receiveLine_[receiveLineLength_] = '\0';

            const size_t requiredOutputSize =
                receiveLineLength_ + 1;

            if (requiredOutputSize > outputSize)
            {
                Logger::errorf(
                    "Printer response needs %u bytes, output buffer has %u bytes",
                    static_cast<unsigned int>(requiredOutputSize),
                    static_cast<unsigned int>(outputSize)
                );

                clearReceiveLine();
                return false;
            }

            memcpy(
                output,
                receiveLine_,
                requiredOutputSize
            );

            clearReceiveLine();
            return true;
        }

        if (receivedCharacter == '\0')
        {
            continue;
        }

        if (receiveLineOverflow_)
        {
            continue;
        }

        if (
            receiveLineLength_ >=
            INTERNAL_RX_LINE_SIZE - 1
        )
        {
            receiveLineOverflow_ = true;
            continue;
        }

        receiveLine_[receiveLineLength_] =
            receivedCharacter;

        ++receiveLineLength_;
    }

    return false;
}

bool UsbPrinter::writeLine(const char* line)
{
    if (line == nullptr)
    {
        Logger::error("Cannot send a null printer command");
        return false;
    }

    if (!connected())
    {
        Logger::warning(
            "Cannot send printer command while disconnected"
        );

        return false;
    }

    size_t commandLength = strlen(line);

    while (
        commandLength > 0 &&
        (
            line[commandLength - 1] == '\r' ||
            line[commandLength - 1] == '\n'
        )
    )
    {
        --commandLength;
    }

    if (commandLength == 0)
    {
        Logger::warning("Cannot send an empty printer command");
        return false;
    }

    const size_t transmissionLength =
        commandLength + 1;

    if (transmissionLength > TX_QUEUE_BUFFER_SIZE)
    {
        Logger::errorf(
            "Printer command is too long: %u bytes",
            static_cast<unsigned int>(commandLength)
        );

        return false;
    }

    uint8_t transmitBuffer[TX_QUEUE_BUFFER_SIZE];

    memcpy(
        transmitBuffer,
        line,
        commandLength
    );

    transmitBuffer[commandLength] = '\n';

    const size_t bytesWritten = serial_.write(
        transmitBuffer,
        transmissionLength
    );

    if (bytesWritten != transmissionLength)
    {
        Logger::warningf(
            "USB printer write incomplete: %u of %u bytes accepted",
            static_cast<unsigned int>(bytesWritten),
            static_cast<unsigned int>(transmissionLength)
        );

        return false;
    }

#if DEBUG_USB
    char logBuffer[TX_QUEUE_BUFFER_SIZE];

    memcpy(
        logBuffer,
        line,
        commandLength
    );

    logBuffer[commandLength] = '\0';

    Logger::tx(logBuffer);
#endif

    return true;
}

bool UsbPrinter::writeLine(const String& line)
{
    return writeLine(line.c_str());
}

bool UsbPrinter::flush(uint32_t timeoutMs)
{
    if (!connected())
    {
        return false;
    }

    if (!usbHost_.serialWriteQueueReady())
    {
        return true;
    }

    const bool flushSuccessful =
        usbHost_.serialWriteFlush(timeoutMs);

    if (!flushSuccessful)
    {
        Logger::warningf(
            "USB printer TX queue did not empty within %lu ms",
            static_cast<unsigned long>(timeoutMs)
        );
    }

    return flushSuccessful;
}

void UsbPrinter::clearReceiveLine()
{
    receiveLineLength_ = 0;
    receiveLineOverflow_ = false;
    receiveLine_[0] = '\0';
}

uint32_t UsbPrinter::baudRate() const
{
    return baudRate_;
}

bool UsbPrinter::startWriteQueue()
{
    if (!connected())
    {
        Logger::warning(
            "Cannot start USB TX queue while printer is disconnected"
        );

        return false;
    }

    if (usbHost_.serialWriteQueueReady())
    {
        return true;
    }

    const bool queueStarted =
        usbHost_.serialWriteQueueBegin(
            TX_QUEUE_DEPTH,
            TX_QUEUE_BUFFER_SIZE
        );

    if (!queueStarted)
    {
        Logger::error(
            "Failed to create bounded USB printer TX queue"
        );

        return false;
    }

    Logger::infof(
        "USB printer TX queue started: depth=%u, buffer=%u bytes",
        static_cast<unsigned int>(TX_QUEUE_DEPTH),
        static_cast<unsigned int>(TX_QUEUE_BUFFER_SIZE)
    );

    return true;
}

void UsbPrinter::updateConnectionState()
{
    const bool currentConnectionState =
        serial_.connected();

    if (
        currentConnectionState ==
        previousConnectionState_
    )
    {
        return;
    }

    previousConnectionState_ =
        currentConnectionState;

    if (!currentConnectionState)
    {
        clearReceiveLine();

        Logger::warning("USB printer disconnected");
        return;
    }

    clearReceiveLine();

    Logger::info("USB printer connected");

    if (!serial_.setBaudRate(baudRate_))
    {
        Logger::warning(
            "Could not apply printer baud rate after connection"
        );
    }
    else
    {
        Logger::infof(
            "Printer baud rate configured to %lu",
            static_cast<unsigned long>(baudRate_)
        );
    }

    if (!serial_.setDtr(true))
    {
        Logger::warning(
            "Could not enable printer DTR after connection"
        );
    }

    if (!serial_.setRts(false))
    {
        Logger::warning(
            "Could not disable printer RTS after connection"
        );
    }

    if (!startWriteQueue())
    {
        Logger::error(
            "Could not initialize USB printer TX queue after connection"
        );
    }
}