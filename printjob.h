#pragma once

#include <Arduino.h>

enum class PrintState : uint8_t
{
    Idle,
    Preparing,
    Printing,
    Paused,
    Finishing,
    Finished,
    Stopped,
    Error
};

enum class PrintFailureReason : uint8_t
{
    None,
    FileOpenFailed,
    FileReadFailed,
    PrinterDisconnected,
    CommunicationTimeout,
    ProtocolError,
    ResendUnavailable,
    TransmissionFailed,
    UserStopped,
    InternalError
};

class PrintJob
{
public:
    PrintJob();

    void reset();

    void begin(
        const char* filePath,
        uint32_t totalFileBytes
    );

    void markPreparing();
    void markPrinting();
    void markPaused();
    void markFinishing();
    void markFinished();
    void markStopped();

    void markError(
        PrintFailureReason reason,
        const char* message
    );

    void updateFilePosition(uint32_t filePosition);

    void commandSent();
    void commandAcknowledged();
    void commandResent();

    PrintState state() const;
    PrintFailureReason failureReason() const;

    const char* filePath() const;
    const char* errorMessage() const;

    bool isActive() const;
    bool isPrinting() const;
    bool isPaused() const;
    bool isFinished() const;
    bool hasError() const;

    uint32_t totalFileBytes() const;
    uint32_t processedFileBytes() const;

    uint32_t commandsSent() const;
    uint32_t commandsAcknowledged() const;
    uint32_t commandsResent() const;

    uint32_t startTimeMs() const;
    uint32_t finishTimeMs() const;
    uint32_t elapsedTimeMs() const;

    float progressPercent() const;

    const char* stateName() const;
    const char* failureReasonName() const;

private:
    static constexpr size_t FILE_PATH_SIZE = 128;
    static constexpr size_t ERROR_MESSAGE_SIZE = 160;

    PrintState state_;
    PrintFailureReason failureReason_;

    char filePath_[FILE_PATH_SIZE];
    char errorMessage_[ERROR_MESSAGE_SIZE];

    uint32_t totalFileBytes_;
    uint32_t processedFileBytes_;

    uint32_t commandsSent_;
    uint32_t commandsAcknowledged_;
    uint32_t commandsResent_;

    uint32_t startTimeMs_;
    uint32_t finishTimeMs_;

    void copyText(
        char* destination,
        size_t destinationSize,
        const char* source
    );
};