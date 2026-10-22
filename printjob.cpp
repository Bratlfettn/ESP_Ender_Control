#include "PrintJob.h"

#include "Logger.h"

#include <cstring>

PrintJob::PrintJob()
    : state_(PrintState::Idle),
      failureReason_(PrintFailureReason::None),
      filePath_{0},
      errorMessage_{0},
      totalFileBytes_(0),
      processedFileBytes_(0),
      commandsSent_(0),
      commandsAcknowledged_(0),
      commandsResent_(0),
      startTimeMs_(0),
      finishTimeMs_(0)
{
}

void PrintJob::reset()
{
    state_ = PrintState::Idle;
    failureReason_ = PrintFailureReason::None;

    filePath_[0] = '\0';
    errorMessage_[0] = '\0';

    totalFileBytes_ = 0;
    processedFileBytes_ = 0;

    commandsSent_ = 0;
    commandsAcknowledged_ = 0;
    commandsResent_ = 0;

    startTimeMs_ = 0;
    finishTimeMs_ = 0;
}

void PrintJob::begin(
    const char* filePath,
    uint32_t totalFileBytes
)
{
    reset();

    copyText(
        filePath_,
        sizeof(filePath_),
        filePath
    );

    totalFileBytes_ = totalFileBytes;
    processedFileBytes_ = 0;

    startTimeMs_ = millis();
    finishTimeMs_ = 0;

    state_ = PrintState::Preparing;

    Logger::infof(
        "Print job created for file: %s",
        filePath_
    );

    Logger::infof(
        "Print job file size: %lu bytes",
        static_cast<unsigned long>(totalFileBytes_)
    );
}

void PrintJob::markPreparing()
{
    if (
        state_ == PrintState::Error ||
        state_ == PrintState::Finished ||
        state_ == PrintState::Stopped
    )
    {
        return;
    }

    state_ = PrintState::Preparing;

    Logger::info("Print job is preparing");
}

void PrintJob::markPrinting()
{
    if (
        state_ == PrintState::Error ||
        state_ == PrintState::Finished ||
        state_ == PrintState::Stopped
    )
    {
        return;
    }

    state_ = PrintState::Printing;

    Logger::info("Print job is printing");
}

void PrintJob::markPaused()
{
    if (state_ != PrintState::Printing)
    {
        return;
    }

    state_ = PrintState::Paused;

    Logger::info("Print job paused");
}

void PrintJob::markFinishing()
{
    if (
        state_ != PrintState::Printing &&
        state_ != PrintState::Paused
    )
    {
        return;
    }

    state_ = PrintState::Finishing;

    Logger::info(
        "G-code file fully read, waiting for acknowledgements"
    );
}

void PrintJob::markFinished()
{
    if (
        state_ == PrintState::Error ||
        state_ == PrintState::Stopped
    )
    {
        return;
    }

    state_ = PrintState::Finished;
    failureReason_ = PrintFailureReason::None;

    processedFileBytes_ = totalFileBytes_;
    finishTimeMs_ = millis();

    Logger::info("Print job finished successfully");

    Logger::infof(
        "Commands sent: %lu",
        static_cast<unsigned long>(commandsSent_)
    );

    Logger::infof(
        "Commands acknowledged: %lu",
        static_cast<unsigned long>(commandsAcknowledged_)
    );

    Logger::infof(
        "Commands resent: %lu",
        static_cast<unsigned long>(commandsResent_)
    );

    Logger::infof(
        "Print duration: %lu seconds",
        static_cast<unsigned long>(
            elapsedTimeMs() / 1000UL
        )
    );
}

void PrintJob::markStopped()
{
    if (
        state_ == PrintState::Finished ||
        state_ == PrintState::Error ||
        state_ == PrintState::Stopped
    )
    {
        return;
    }

    state_ = PrintState::Stopped;
    failureReason_ = PrintFailureReason::UserStopped;

    copyText(
        errorMessage_,
        sizeof(errorMessage_),
        "Print stopped by user"
    );

    finishTimeMs_ = millis();

    Logger::warning("Print job stopped by user");
}

void PrintJob::markError(
    PrintFailureReason reason,
    const char* message
)
{
    if (
        state_ == PrintState::Finished ||
        state_ == PrintState::Stopped
    )
    {
        return;
    }

    state_ = PrintState::Error;
    failureReason_ = reason;

    copyText(
        errorMessage_,
        sizeof(errorMessage_),
        message
    );

    finishTimeMs_ = millis();

    Logger::errorf(
        "Print job failed: %s",
        errorMessage_
    );

    Logger::errorf(
        "Failure reason: %s",
        failureReasonName()
    );
}

void PrintJob::updateFilePosition(uint32_t filePosition)
{
    if (filePosition > totalFileBytes_)
    {
        processedFileBytes_ = totalFileBytes_;
        return;
    }

    processedFileBytes_ = filePosition;
}

void PrintJob::commandSent()
{
    ++commandsSent_;
}

void PrintJob::commandAcknowledged()
{
    ++commandsAcknowledged_;
}

void PrintJob::commandResent()
{
    ++commandsResent_;
}

PrintState PrintJob::state() const
{
    return state_;
}

PrintFailureReason PrintJob::failureReason() const
{
    return failureReason_;
}

const char* PrintJob::filePath() const
{
    return filePath_;
}

const char* PrintJob::errorMessage() const
{
    return errorMessage_;
}

bool PrintJob::isActive() const
{
    return
        state_ == PrintState::Preparing ||
        state_ == PrintState::Printing ||
        state_ == PrintState::Paused ||
        state_ == PrintState::Finishing;
}

bool PrintJob::isPrinting() const
{
    return state_ == PrintState::Printing;
}

bool PrintJob::isPaused() const
{
    return state_ == PrintState::Paused;
}

bool PrintJob::isFinished() const
{
    return state_ == PrintState::Finished;
}

bool PrintJob::hasError() const
{
    return state_ == PrintState::Error;
}

uint32_t PrintJob::totalFileBytes() const
{
    return totalFileBytes_;
}

uint32_t PrintJob::processedFileBytes() const
{
    return processedFileBytes_;
}

uint32_t PrintJob::commandsSent() const
{
    return commandsSent_;
}

uint32_t PrintJob::commandsAcknowledged() const
{
    return commandsAcknowledged_;
}

uint32_t PrintJob::commandsResent() const
{
    return commandsResent_;
}

uint32_t PrintJob::startTimeMs() const
{
    return startTimeMs_;
}

uint32_t PrintJob::finishTimeMs() const
{
    return finishTimeMs_;
}

uint32_t PrintJob::elapsedTimeMs() const
{
    if (startTimeMs_ == 0)
    {
        return 0;
    }

    if (finishTimeMs_ != 0)
    {
        return finishTimeMs_ - startTimeMs_;
    }

    return millis() - startTimeMs_;
}

float PrintJob::progressPercent() const
{
    if (totalFileBytes_ == 0)
    {
        if (state_ == PrintState::Finished)
        {
            return 100.0f;
        }

        return 0.0f;
    }

    float progress =
        100.0f *
        static_cast<float>(processedFileBytes_) /
        static_cast<float>(totalFileBytes_);

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

const char* PrintJob::stateName() const
{
    switch (state_)
    {
        case PrintState::Idle:
            return "Idle";

        case PrintState::Preparing:
            return "Preparing";

        case PrintState::Printing:
            return "Printing";

        case PrintState::Paused:
            return "Paused";

        case PrintState::Finishing:
            return "Finishing";

        case PrintState::Finished:
            return "Finished";

        case PrintState::Stopped:
            return "Stopped";

        case PrintState::Error:
            return "Error";

        default:
            return "Unknown";
    }
}

const char* PrintJob::failureReasonName() const
{
    switch (failureReason_)
    {
        case PrintFailureReason::None:
            return "None";

        case PrintFailureReason::FileOpenFailed:
            return "File open failed";

        case PrintFailureReason::FileReadFailed:
            return "File read failed";

        case PrintFailureReason::PrinterDisconnected:
            return "Printer disconnected";

        case PrintFailureReason::CommunicationTimeout:
            return "Communication timeout";

        case PrintFailureReason::ProtocolError:
            return "Protocol error";

        case PrintFailureReason::ResendUnavailable:
            return "Requested resend is unavailable";

        case PrintFailureReason::TransmissionFailed:
            return "Transmission failed";

        case PrintFailureReason::UserStopped:
            return "Stopped by user";

        case PrintFailureReason::InternalError:
            return "Internal error";

        default:
            return "Unknown";
    }
}

void PrintJob::copyText(
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

    const size_t sourceLength = strlen(source);

    size_t copyLength = sourceLength;

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