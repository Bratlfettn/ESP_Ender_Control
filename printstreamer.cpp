#include "PrintStreamer.h"

#include "Config.h"
#include "Logger.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

PrintStreamer::PrintStreamer(
    UsbPrinter& printer
)
    : printer_(printer),
      gcodeFile_(),
      protocol_(),
      history_(),
      printJob_(),
      state_(StreamerState::Idle),
      stateBeforeResend_(StreamerState::Idle),
      positioningMode_(PositioningMode::Absolute),
      inFlightLines_{0},
      inFlightHead_(0),
      inFlightCount_(0),
      pendingCommandValid_(false),
      fileExhausted_(false),
      currentInFlightCommandIsControl_(false),
      resendReturnCommandWasControl_(false),
      pendingRawCommand_{0},
      pendingFormattedCommand_{0},
      pendingLineNumber_(0),
      pendingFileStartPosition_(0),
      pendingFileEndPosition_(0),
      resendNextLine_(0),
      resendLastLine_(0),
      lastPrinterResponseMs_(0),
      lastAcknowledgementMs_(0),
      lastLoopExecutionMs_(0),
      unexpectedOkCount_(0),
      transmissionFailureCount_(0),
      currentZPositionMm_(0.0f),
      nozzleTemperatureC_(0.0f),
      nozzleTargetC_(0.0f),
      bedTemperatureC_(0.0f),
      bedTargetC_(0.0f),
      savedNozzleTargetC_(0.0f),
      appliedLiftDistanceMm_(0.0f),
      currentZPositionKnown_(false),
      nozzleTemperatureKnown_(false),
      nozzleTargetKnown_(false),
      bedTemperatureKnown_(false),
      bedTargetKnown_(false),
      lastTemperatureUpdateMs_(0)
{
}

bool PrintStreamer::start(
    fs::FS& fileSystem,
    const char* filePath
)
{
    if (active())
    {
        Logger::error(
            "Cannot start a new print while another print is active"
        );

        return false;
    }

    resetRuntimeState();

    if (!printer_.connected())
    {
        printJob_.reset();

        printJob_.markError(
            PrintFailureReason::PrinterDisconnected,
            "Printer is not connected"
        );

        state_ =
            StreamerState::Error;

        return false;
    }

    if (
        !gcodeFile_.open(
            fileSystem,
            filePath
        )
    )
    {
        printJob_.reset();

        printJob_.markError(
            PrintFailureReason::FileOpenFailed,
            "Could not open G-code file"
        );

        state_ =
            StreamerState::Error;

        return false;
    }

    printJob_.begin(
        filePath,
        gcodeFile_.size()
    );

    protocol_.reset(1);
    history_.clear();

    clearInFlight();
    clearPendingCommand();

    fileExhausted_ = false;

    lastPrinterResponseMs_ =
        millis();

    lastAcknowledgementMs_ =
        millis();

    lastLoopExecutionMs_ = 0;

    state_ =
        StreamerState::SendLineNumberReset;

    Logger::info(
        "Print streamer started"
    );

    return true;
}

void PrintStreamer::loop()
{
    printer_.loop();

    processPrinterInput();

    if (!active())
    {
        return;
    }

    checkConnection();

    if (!active())
    {
        return;
    }

    checkCommunicationTimeout();

    if (!active())
    {
        return;
    }

    const uint32_t currentTime =
        millis();

    if (
        currentTime -
            lastLoopExecutionMs_ <
        STREAMER_INTERVAL_MS
    )
    {
        return;
    }

    lastLoopExecutionMs_ =
        currentTime;

    switch (state_)
    {
        case StreamerState::SendLineNumberReset:
            sendLineNumberReset();
            break;

        case StreamerState::WaitForLineNumberReset:
            break;

        case StreamerState::Streaming:
            fillTransmissionWindow();
            break;

        case StreamerState::PauseRequested:
            processPauseRequest();
            break;

        case StreamerState::PauseWaitForMotion:
        case StreamerState::PauseSetRelativeMode:
        case StreamerState::PauseLiftZ:
        case StreamerState::PauseRestorePositioningMode:
        case StreamerState::PauseDisableNozzle:
            processPauseSequence();
            break;

        case StreamerState::Paused:
            break;

        case StreamerState::ResumeHeatNozzle:
        case StreamerState::ResumeSetRelativeMode:
        case StreamerState::ResumeLowerZ:
        case StreamerState::ResumeRestorePositioningMode:
            processResumeSequence();
            break;

        case StreamerState::StopRequested:
            processStopRequest();
            break;

        case StreamerState::StopWaitForMotion:
        case StreamerState::StopSetRelativeMode:
        case StreamerState::StopLiftZ:
        case StreamerState::StopRestorePositioningMode:
        case StreamerState::StopDisableNozzle:
        case StreamerState::StopDisableBed:
        case StreamerState::StopDisableFan:
        case StreamerState::StopDisableMotors:
            processStopSequence();
            break;

        case StreamerState::Resending:
            sendNextResendCommand();
            break;

        case StreamerState::Draining:
            checkForCompletion();
            break;

        case StreamerState::Idle:
        case StreamerState::Finished:
        case StreamerState::Stopped:
        case StreamerState::Error:
        default:
            break;
    }
}

void PrintStreamer::pause()
{
    if (
        state_ !=
        StreamerState::Streaming
    )
    {
        return;
    }

    if (
        nozzleTargetKnown_ &&
        nozzleTargetC_ >=
            MINIMUM_VALID_NOZZLE_TARGET_C &&
        nozzleTargetC_ <=
            MAXIMUM_VALID_NOZZLE_TARGET_C
    )
    {
        savedNozzleTargetC_ =
            nozzleTargetC_;
    }
    else
    {
        savedNozzleTargetC_ =
            0.0f;
    }

    appliedLiftDistanceMm_ =
        calculateSafeLiftDistance();

    transmissionFailureCount_ = 0;

    state_ =
        StreamerState::PauseRequested;

    Logger::info(
        "Safe pause requested"
    );
}

void PrintStreamer::resume()
{
    if (
        state_ !=
        StreamerState::Paused
    )
    {
        return;
    }

    if (
        savedNozzleTargetC_ <
            MINIMUM_VALID_NOZZLE_TARGET_C ||
        savedNozzleTargetC_ >
            MAXIMUM_VALID_NOZZLE_TARGET_C
    )
    {
        fail(
            PrintFailureReason::InternalError,
            "Cannot resume because no valid nozzle target was saved"
        );

        return;
    }

    transmissionFailureCount_ = 0;

    lastPrinterResponseMs_ =
        millis();

    lastAcknowledgementMs_ =
        millis();

    state_ =
        StreamerState::ResumeHeatNozzle;

    Logger::info(
        "Resume sequence started"
    );
}

void PrintStreamer::stop()
{
    if (!active())
    {
        return;
    }

    if (stopInProgress())
    {
        return;
    }

    if (
        state_ ==
        StreamerState::Paused
    )
    {
        transmissionFailureCount_ = 0;

        state_ =
            StreamerState::StopDisableBed;

        Logger::warning(
            "Permanent stop started from paused state"
        );

        return;
    }

    if (
        safePauseRequested() ||
        resumeInProgress() ||
        state_ ==
            StreamerState::Resending
    )
    {
        Logger::warning(
            "Cannot stop during a pause, resume, or resend sequence"
        );

        return;
    }

    if (
        state_ !=
            StreamerState::Streaming &&
        state_ !=
            StreamerState::Draining
    )
    {
        return;
    }

    appliedLiftDistanceMm_ =
        calculateSafeLiftDistance();

    transmissionFailureCount_ = 0;

    state_ =
        StreamerState::StopRequested;

    Logger::warning(
        "Permanent print stop requested"
    );
}

bool PrintStreamer::active() const
{
    return
        state_ ==
            StreamerState::SendLineNumberReset ||
        state_ ==
            StreamerState::WaitForLineNumberReset ||
        state_ ==
            StreamerState::Streaming ||
        safePauseRequested() ||
        state_ ==
            StreamerState::Paused ||
        resumeInProgress() ||
        stopInProgress() ||
        state_ ==
            StreamerState::Resending ||
        state_ ==
            StreamerState::Draining;
}

bool PrintStreamer::waitingForAcknowledgement() const
{
    return
        state_ ==
            StreamerState::WaitForLineNumberReset ||
        inFlightCount_ > 0;
}

bool PrintStreamer::safePauseRequested() const
{
    return
        state_ ==
            StreamerState::PauseRequested ||
        state_ ==
            StreamerState::PauseWaitForMotion ||
        state_ ==
            StreamerState::PauseSetRelativeMode ||
        state_ ==
            StreamerState::PauseLiftZ ||
        state_ ==
            StreamerState::PauseRestorePositioningMode ||
        state_ ==
            StreamerState::PauseDisableNozzle;
}

bool PrintStreamer::safelyPaused() const
{
    return
        state_ ==
        StreamerState::Paused;
}

bool PrintStreamer::resumeInProgress() const
{
    return
        state_ ==
            StreamerState::ResumeHeatNozzle ||
        state_ ==
            StreamerState::ResumeSetRelativeMode ||
        state_ ==
            StreamerState::ResumeLowerZ ||
        state_ ==
            StreamerState::ResumeRestorePositioningMode;
}

bool PrintStreamer::stopInProgress() const
{
    return
        state_ ==
            StreamerState::StopRequested ||
        state_ ==
            StreamerState::StopWaitForMotion ||
        state_ ==
            StreamerState::StopSetRelativeMode ||
        state_ ==
            StreamerState::StopLiftZ ||
        state_ ==
            StreamerState::StopRestorePositioningMode ||
        state_ ==
            StreamerState::StopDisableNozzle ||
        state_ ==
            StreamerState::StopDisableBed ||
        state_ ==
            StreamerState::StopDisableFan ||
        state_ ==
            StreamerState::StopDisableMotors;
}

float PrintStreamer::savedNozzleTarget() const
{
    return savedNozzleTargetC_;
}

float PrintStreamer::appliedLiftDistance() const
{
    return appliedLiftDistanceMm_;
}

size_t PrintStreamer::inFlightCount() const
{
    return inFlightCount_;
}

bool PrintStreamer::hasTemperatureData() const
{
    return
        nozzleTemperatureKnown_ ||
        bedTemperatureKnown_ ||
        nozzleTargetKnown_ ||
        bedTargetKnown_;
}

float PrintStreamer::nozzleTemperatureC() const
{
    return nozzleTemperatureC_;
}

float PrintStreamer::nozzleTargetC() const
{
    return nozzleTargetC_;
}

float PrintStreamer::bedTemperatureC() const
{
    return bedTemperatureC_;
}

float PrintStreamer::bedTargetC() const
{
    return bedTargetC_;
}

uint32_t PrintStreamer::lastTemperatureUpdateMs() const
{
    return lastTemperatureUpdateMs_;
}

uint32_t PrintStreamer::temperatureDataAgeMs() const
{
    if (
        !hasTemperatureData() ||
        lastTemperatureUpdateMs_ == 0
    )
    {
        return 0;
    }

    return
        millis() -
        lastTemperatureUpdateMs_;
}

const PrintJob& PrintStreamer::job() const
{
    return printJob_;
}

PrintJob& PrintStreamer::job()
{
    return printJob_;
}

void PrintStreamer::processPrinterInput()
{
    char response[
        MAX_GCODE_LINE_LENGTH
    ];

    while (
        printer_.readLine(
            response,
            sizeof(response)
        )
    )
    {
        lastPrinterResponseMs_ =
            millis();

#if DEBUG_USB
        Logger::rx(response);
#endif

        processPrinterResponse(
            response
        );
    }
}

void PrintStreamer::processPrinterResponse(
    const char* response
)
{
    if (response == nullptr)
    {
        return;
    }

    const char* trimmedResponse =
        skipWhitespace(response);

    if (
        trimmedResponse[0] ==
        '\0'
    )
    {
        return;
    }

    updateTemperaturesFromResponse(
        trimmedResponse
    );

    if (
        startsWithIgnoreCase(
            trimmedResponse,
            "ok"
        )
    )
    {
        processOkResponse(
            trimmedResponse
        );

        return;
    }

    if (
        startsWithIgnoreCase(
            trimmedResponse,
            "busy:"
        ) ||
        containsIgnoreCase(
            trimmedResponse,
            "busy: processing"
        ) ||
        containsIgnoreCase(
            trimmedResponse,
            "busy: paused for user"
        )
    )
    {
        processBusyResponse(
            trimmedResponse
        );

        return;
    }

    uint32_t requestedLine = 0;

    if (
        parseResendLineNumber(
            trimmedResponse,
            requestedLine
        )
    )
    {
        processResendResponse(
            trimmedResponse
        );

        return;
    }

    if (
        startsWithIgnoreCase(
            trimmedResponse,
            "error:"
        ) ||
        startsWithIgnoreCase(
            trimmedResponse,
            "err:"
        ) ||
        containsIgnoreCase(
            trimmedResponse,
            "checksum mismatch"
        ) ||
        containsIgnoreCase(
            trimmedResponse,
            "line number"
        )
    )
    {
        processErrorResponse(
            trimmedResponse
        );
    }
}

void PrintStreamer::processOkResponse(
    const char* response
)
{
    static_cast<void>(
        response
    );

    lastPrinterResponseMs_ =
        millis();

    lastAcknowledgementMs_ =
        millis();

    unexpectedOkCount_ = 0;
    transmissionFailureCount_ = 0;

    if (
        state_ ==
        StreamerState::WaitForLineNumberReset
    )
    {
        protocol_.reset(1);

        state_ =
            StreamerState::Streaming;

        printJob_.markPrinting();

        return;
    }

    uint32_t acknowledgedLine = 0;

    if (
        !dequeueInFlight(
            acknowledgedLine
        )
    )
    {
        ++unexpectedOkCount_;

        return;
    }

    history_.markAcknowledged(
        acknowledgedLine
    );

    printJob_.commandAcknowledged();

    if (
        state_ ==
        StreamerState::Resending
    )
    {
        return;
    }

    if (currentInFlightCommandIsControl_)
    {
        currentInFlightCommandIsControl_ =
            false;

        advanceControlSequenceAfterAcknowledgement();

        return;
    }

    if (
        state_ ==
            StreamerState::PauseRequested ||
        state_ ==
            StreamerState::StopRequested
    )
    {
        return;
    }

    if (
        state_ ==
        StreamerState::Draining
    )
    {
        checkForCompletion();

        return;
    }

    if (
        state_ ==
            StreamerState::Streaming &&
        fileExhausted_ &&
        !pendingCommandValid_ &&
        inFlightCount_ == 0
    )
    {
        checkForCompletion();
    }
}

void PrintStreamer::processBusyResponse(
    const char* response
)
{
    static_cast<void>(
        response
    );

    lastPrinterResponseMs_ =
        millis();
}

void PrintStreamer::processResendResponse(
    const char* response
)
{
    uint32_t requestedLine = 0;

    if (
        !parseResendLineNumber(
            response,
            requestedLine
        )
    )
    {
        fail(
            PrintFailureReason::ProtocolError,
            "Could not parse Marlin resend request"
        );

        return;
    }

    const uint32_t newestStoredLine =
        history_.newestLineNumber();

    if (
        requestedLine == 0 ||
        newestStoredLine == 0 ||
        requestedLine >
            newestStoredLine ||
        !history_.contains(
            requestedLine
        )
    )
    {
        fail(
            PrintFailureReason::ResendUnavailable,
            "Marlin requested an unavailable protocol line"
        );

        return;
    }

    if (
        state_ !=
        StreamerState::Resending
    )
    {
        stateBeforeResend_ =
            state_;

        resendReturnCommandWasControl_ =
            currentInFlightCommandIsControl_;
    }

    clearInFlight();

    currentInFlightCommandIsControl_ =
        false;

    resendNextLine_ =
        requestedLine;

    resendLastLine_ =
        newestStoredLine;

    state_ =
        StreamerState::Resending;

    lastPrinterResponseMs_ =
        millis();

    lastAcknowledgementMs_ =
        millis();

    transmissionFailureCount_ = 0;
}

void PrintStreamer::processErrorResponse(
    const char* response
)
{
    uint32_t requestedLine = 0;

    if (
        parseResendLineNumber(
            response,
            requestedLine
        )
    )
    {
        processResendResponse(
            response
        );

        return;
    }

    if (
        containsIgnoreCase(
            response,
            "checksum"
        ) ||
        containsIgnoreCase(
            response,
            "line number"
        ) ||
        containsIgnoreCase(
            response,
            "last line"
        )
    )
    {
        return;
    }

    Logger::warningf(
        "Marlin error response: %s",
        response
    );
}

void PrintStreamer::sendLineNumberReset()
{
    char resetCommand[
        MAX_GCODE_LINE_LENGTH
    ];

    if (
        !protocol_.buildLineNumberReset(
            resetCommand,
            sizeof(resetCommand)
        )
    )
    {
        fail(
            PrintFailureReason::ProtocolError,
            "Could not build line-number reset"
        );

        return;
    }

    if (
        !printer_.writeLine(
            resetCommand
        )
    )
    {
        ++transmissionFailureCount_;

        if (
            transmissionFailureCount_ >=
            MAX_CONTROL_TRANSMISSION_FAILURES
        )
        {
            fail(
                PrintFailureReason::TransmissionFailed,
                "Could not send line-number reset"
            );
        }

        return;
    }

    transmissionFailureCount_ = 0;

    lastPrinterResponseMs_ =
        millis();

    lastAcknowledgementMs_ =
        millis();

    state_ =
        StreamerState::WaitForLineNumberReset;
}

void PrintStreamer::fillTransmissionWindow()
{
    if (
        state_ !=
        StreamerState::Streaming
    )
    {
        return;
    }

    while (
        state_ ==
            StreamerState::Streaming &&
        inFlightCount_ <
            MAX_INFLIGHT_COMMANDS
    )
    {
        if (!pendingCommandValid_)
        {
            prepareNextCommand();
        }

        if (!active())
        {
            return;
        }

        if (
            fileExhausted_ &&
            !pendingCommandValid_
        )
        {
            if (
                inFlightCount_ == 0
            )
            {
                checkForCompletion();
            }
            else
            {
                state_ =
                    StreamerState::Draining;

                printJob_.markFinishing();
            }

            return;
        }

        if (!pendingCommandValid_)
        {
            return;
        }

        const size_t countBefore =
            inFlightCount_;

        sendPendingCommand();

        if (
            pendingCommandValid_ &&
            inFlightCount_ ==
                countBefore
        )
        {
            return;
        }
    }
}

void PrintStreamer::prepareNextCommand()
{
    if (
        pendingCommandValid_ ||
        fileExhausted_
    )
    {
        return;
    }

    pendingRawCommand_[0] = '\0';
    pendingFormattedCommand_[0] = '\0';

    if (
        !gcodeFile_.nextLine(
            pendingRawCommand_,
            sizeof(pendingRawCommand_)
        )
    )
    {
        if (gcodeFile_.hasError())
        {
            fail(
                PrintFailureReason::FileReadFailed,
                "Failed while reading G-code file"
            );

            return;
        }

        if (gcodeFile_.eof())
        {
            fileExhausted_ = true;

            printJob_.updateFilePosition(
                gcodeFile_.size()
            );
        }

        return;
    }

    pendingFileStartPosition_ =
        gcodeFile_.currentLineStartPosition();

    pendingFileEndPosition_ =
        gcodeFile_.currentLineEndPosition();

    if (
        !protocol_.buildCommand(
            pendingRawCommand_,
            pendingFormattedCommand_,
            sizeof(pendingFormattedCommand_),
            pendingLineNumber_
        )
    )
    {
        fail(
            PrintFailureReason::ProtocolError,
            "Failed to build numbered command"
        );

        return;
    }

    pendingCommandValid_ = true;
}

void PrintStreamer::sendPendingCommand()
{
    if (
        !pendingCommandValid_ ||
        inFlightCount_ >=
            MAX_INFLIGHT_COMMANDS
    )
    {
        return;
    }

    if (
        !history_.store(
            pendingLineNumber_,
            pendingFormattedCommand_,
            pendingFileStartPosition_,
            pendingFileEndPosition_
        )
    )
    {
        fail(
            PrintFailureReason::InternalError,
            "Could not store command history"
        );

        return;
    }

    if (
        !printer_.writeLine(
            pendingFormattedCommand_
        )
    )
    {
        ++transmissionFailureCount_;

        if (
            transmissionFailureCount_ >=
            MAX_CONTROL_TRANSMISSION_FAILURES
        )
        {
            fail(
                PrintFailureReason::TransmissionFailed,
                "Could not send G-code command"
            );
        }

        return;
    }

    transmissionFailureCount_ = 0;

    if (
        !enqueueInFlight(
            pendingLineNumber_
        )
    )
    {
        fail(
            PrintFailureReason::InternalError,
            "Could not queue G-code command"
        );

        return;
    }

    currentInFlightCommandIsControl_ =
        false;

    updateTrackedPrinterState(
        pendingRawCommand_
    );

    printJob_.commandSent();

    printJob_.updateFilePosition(
        pendingFileEndPosition_
    );

    clearPendingCommand();

    lastPrinterResponseMs_ =
        millis();
}

void PrintStreamer::processPauseRequest()
{
    if (
        state_ !=
            StreamerState::PauseRequested ||
        inFlightCount_ > 0
    )
    {
        return;
    }

    state_ =
        StreamerState::PauseWaitForMotion;

    processPauseSequence();
}

void PrintStreamer::processPauseSequence()
{
    if (inFlightCount_ > 0)
    {
        return;
    }

    switch (state_)
    {
        case StreamerState::PauseWaitForMotion:
            sendControlCommand("M400");
            break;

        case StreamerState::PauseSetRelativeMode:
            sendControlCommand("G91");
            break;

        case StreamerState::PauseLiftZ:
        {
            if (
                appliedLiftDistanceMm_ <=
                0.0001f
            )
            {
                state_ =
                    StreamerState::PauseRestorePositioningMode;

                return;
            }

            char command[
                MAX_GCODE_LINE_LENGTH
            ];

            snprintf(
                command,
                sizeof(command),
                "G1 Z%.3f F600",
                static_cast<double>(
                    appliedLiftDistanceMm_
                )
            );

            sendControlCommand(
                command
            );

            break;
        }

        case StreamerState::PauseRestorePositioningMode:
            sendControlCommand(
                positioningMode_ ==
                    PositioningMode::Absolute
                    ? "G90"
                    : "G91"
            );
            break;

        case StreamerState::PauseDisableNozzle:
            sendControlCommand(
                "M104 S0"
            );
            break;

        default:
            break;
    }
}

void PrintStreamer::processResumeSequence()
{
    if (inFlightCount_ > 0)
    {
        return;
    }

    switch (state_)
    {
        case StreamerState::ResumeHeatNozzle:
        {
            char command[
                MAX_GCODE_LINE_LENGTH
            ];

            snprintf(
                command,
                sizeof(command),
                "M109 S%.1f",
                static_cast<double>(
                    savedNozzleTargetC_
                )
            );

            sendControlCommand(
                command
            );

            break;
        }

        case StreamerState::ResumeSetRelativeMode:
            sendControlCommand(
                "G91"
            );
            break;

        case StreamerState::ResumeLowerZ:
        {
            if (
                appliedLiftDistanceMm_ <=
                0.0001f
            )
            {
                state_ =
                    StreamerState::ResumeRestorePositioningMode;

                return;
            }

            char command[
                MAX_GCODE_LINE_LENGTH
            ];

            snprintf(
                command,
                sizeof(command),
                "G1 Z-%.3f F600",
                static_cast<double>(
                    appliedLiftDistanceMm_
                )
            );

            sendControlCommand(
                command
            );

            break;
        }

        case StreamerState::ResumeRestorePositioningMode:
            sendControlCommand(
                positioningMode_ ==
                    PositioningMode::Absolute
                    ? "G90"
                    : "G91"
            );
            break;

        default:
            break;
    }
}

void PrintStreamer::processStopRequest()
{
    if (
        state_ !=
            StreamerState::StopRequested ||
        inFlightCount_ > 0
    )
    {
        return;
    }

    state_ =
        StreamerState::StopWaitForMotion;

    processStopSequence();
}

void PrintStreamer::processStopSequence()
{
    if (inFlightCount_ > 0)
    {
        return;
    }

    switch (state_)
    {
        case StreamerState::StopWaitForMotion:
            sendControlCommand("M400");
            break;

        case StreamerState::StopSetRelativeMode:
            sendControlCommand("G91");
            break;

        case StreamerState::StopLiftZ:
        {
            if (
                appliedLiftDistanceMm_ <=
                0.0001f
            )
            {
                state_ =
                    StreamerState::StopRestorePositioningMode;

                return;
            }

            char command[
                MAX_GCODE_LINE_LENGTH
            ];

            snprintf(
                command,
                sizeof(command),
                "G1 Z%.3f F600",
                static_cast<double>(
                    appliedLiftDistanceMm_
                )
            );

            sendControlCommand(
                command
            );

            break;
        }

        case StreamerState::StopRestorePositioningMode:
            sendControlCommand(
                positioningMode_ ==
                    PositioningMode::Absolute
                    ? "G90"
                    : "G91"
            );
            break;

        case StreamerState::StopDisableNozzle:
            sendControlCommand(
                "M104 S0"
            );
            break;

        case StreamerState::StopDisableBed:
            sendControlCommand(
                "M140 S0"
            );
            break;

        case StreamerState::StopDisableFan:
            sendControlCommand(
                "M107"
            );
            break;

        case StreamerState::StopDisableMotors:
            sendControlCommand(
                "M84"
            );
            break;

        default:
            break;
    }
}

bool PrintStreamer::sendControlCommand(
    const char* command
)
{
    if (
        command == nullptr ||
        inFlightCount_ > 0
    )
    {
        return false;
    }

    char formatted[
        MAX_GCODE_LINE_LENGTH
    ];

    uint32_t lineNumber = 0;

    if (
        !protocol_.buildCommand(
            command,
            formatted,
            sizeof(formatted),
            lineNumber
        )
    )
    {
        fail(
            PrintFailureReason::ProtocolError,
            "Could not build control command"
        );

        return false;
    }

    const uint32_t filePosition =
        gcodeFile_.position();

    if (
        !history_.store(
            lineNumber,
            formatted,
            filePosition,
            filePosition
        )
    )
    {
        fail(
            PrintFailureReason::InternalError,
            "Could not store control command"
        );

        return false;
    }

    if (
        !printer_.writeLine(
            formatted
        )
    )
    {
        ++transmissionFailureCount_;

        if (
            transmissionFailureCount_ >=
            MAX_CONTROL_TRANSMISSION_FAILURES
        )
        {
            fail(
                PrintFailureReason::TransmissionFailed,
                "Could not send control command"
            );
        }

        return false;
    }

    transmissionFailureCount_ = 0;

    if (
        !enqueueInFlight(
            lineNumber
        )
    )
    {
        fail(
            PrintFailureReason::InternalError,
            "Could not queue control command"
        );

        return false;
    }

    currentInFlightCommandIsControl_ =
        true;

    updateTrackedPrinterState(
        command
    );

    printJob_.commandSent();

    lastPrinterResponseMs_ =
        millis();

    lastAcknowledgementMs_ =
        millis();

    return true;
}

void PrintStreamer::advanceControlSequenceAfterAcknowledgement()
{
    switch (state_)
    {
        case StreamerState::PauseWaitForMotion:
            state_ =
                StreamerState::PauseSetRelativeMode;
            break;

        case StreamerState::PauseSetRelativeMode:
            state_ =
                StreamerState::PauseLiftZ;
            break;

        case StreamerState::PauseLiftZ:
            if (currentZPositionKnown_)
            {
                currentZPositionMm_ +=
                    appliedLiftDistanceMm_;
            }

            state_ =
                StreamerState::PauseRestorePositioningMode;
            break;

        case StreamerState::PauseRestorePositioningMode:
            state_ =
                StreamerState::PauseDisableNozzle;
            break;

        case StreamerState::PauseDisableNozzle:
            nozzleTargetC_ = 0.0f;
            nozzleTargetKnown_ = true;

            enterPausedState();

            break;

        case StreamerState::ResumeHeatNozzle:
            nozzleTargetC_ =
                savedNozzleTargetC_;

            nozzleTargetKnown_ = true;

            state_ =
                StreamerState::ResumeSetRelativeMode;
            break;

        case StreamerState::ResumeSetRelativeMode:
            state_ =
                StreamerState::ResumeLowerZ;
            break;

        case StreamerState::ResumeLowerZ:
            if (currentZPositionKnown_)
            {
                currentZPositionMm_ -=
                    appliedLiftDistanceMm_;

                if (
                    currentZPositionMm_ <
                    0.0f
                )
                {
                    currentZPositionMm_ =
                        0.0f;
                }
            }

            state_ =
                StreamerState::ResumeRestorePositioningMode;
            break;

        case StreamerState::ResumeRestorePositioningMode:
            finishResumeSequence();
            break;

        case StreamerState::StopWaitForMotion:
            state_ =
                StreamerState::StopSetRelativeMode;
            break;

        case StreamerState::StopSetRelativeMode:
            state_ =
                StreamerState::StopLiftZ;
            break;

        case StreamerState::StopLiftZ:
            if (currentZPositionKnown_)
            {
                currentZPositionMm_ +=
                    appliedLiftDistanceMm_;
            }

            state_ =
                StreamerState::StopRestorePositioningMode;
            break;

        case StreamerState::StopRestorePositioningMode:
            state_ =
                StreamerState::StopDisableNozzle;
            break;

        case StreamerState::StopDisableNozzle:
            nozzleTargetC_ = 0.0f;
            nozzleTargetKnown_ = true;

            state_ =
                StreamerState::StopDisableBed;
            break;

        case StreamerState::StopDisableBed:
            bedTargetC_ = 0.0f;
            bedTargetKnown_ = true;

            state_ =
                StreamerState::StopDisableFan;
            break;

        case StreamerState::StopDisableFan:
            state_ =
                StreamerState::StopDisableMotors;
            break;

        case StreamerState::StopDisableMotors:
            finishStopSequence();
            break;

        default:
            break;
    }
}

void PrintStreamer::enterPausedState()
{
    state_ =
        StreamerState::Paused;

    printJob_.markPaused();

    Logger::info(
        "Print safely paused"
    );
}

void PrintStreamer::finishResumeSequence()
{
    appliedLiftDistanceMm_ =
        0.0f;

    state_ =
        StreamerState::Streaming;

    printJob_.markPrinting();

    lastPrinterResponseMs_ =
        millis();

    lastAcknowledgementMs_ =
        millis();

    Logger::info(
        "Print resume completed"
    );
}

void PrintStreamer::finishStopSequence()
{
    clearPendingCommand();
    clearInFlight();
    history_.clear();

    gcodeFile_.close();

    appliedLiftDistanceMm_ =
        0.0f;

    state_ =
        StreamerState::Stopped;

    printJob_.markStopped();

    Logger::warning(
        "Print permanently stopped"
    );
}

void PrintStreamer::sendNextResendCommand()
{
    if (
        state_ !=
            StreamerState::Resending ||
        inFlightCount_ > 0
    )
    {
        return;
    }

    if (
        resendNextLine_ >
        resendLastLine_
    )
    {
        finishResendSequence();

        return;
    }

    const CommandHistory::Entry* entry =
        history_.find(
            resendNextLine_
        );

    if (entry == nullptr)
    {
        fail(
            PrintFailureReason::ResendUnavailable,
            "Requested resend command is missing"
        );

        return;
    }

    const uint32_t currentLine =
        resendNextLine_;

    if (
        !printer_.writeLine(
            entry->command
        )
    )
    {
        ++transmissionFailureCount_;

        if (
            transmissionFailureCount_ >=
            MAX_CONTROL_TRANSMISSION_FAILURES
        )
        {
            fail(
                PrintFailureReason::TransmissionFailed,
                "Could not send resend command"
            );
        }

        return;
    }

    transmissionFailureCount_ = 0;

    if (
        !enqueueInFlight(
            currentLine
        )
    )
    {
        fail(
            PrintFailureReason::InternalError,
            "Could not queue resend command"
        );

        return;
    }

    printJob_.commandResent();

    ++resendNextLine_;

    lastPrinterResponseMs_ =
        millis();
}

void PrintStreamer::finishResendSequence()
{
    resendNextLine_ = 0;
    resendLastLine_ = 0;

    state_ =
        stateBeforeResend_;

    if (resendReturnCommandWasControl_)
    {
        resendReturnCommandWasControl_ =
            false;

        advanceControlSequenceAfterAcknowledgement();

        return;
    }

    if (
        state_ ==
        StreamerState::Draining
    )
    {
        checkForCompletion();
    }
}

bool PrintStreamer::enqueueInFlight(
    uint32_t lineNumber
)
{
    if (
        inFlightCount_ >=
        MAX_INFLIGHT_COMMANDS
    )
    {
        return false;
    }

    const size_t index =
        (
            inFlightHead_ +
            inFlightCount_
        ) %
        MAX_INFLIGHT_COMMANDS;

    inFlightLines_[index] =
        lineNumber;

    ++inFlightCount_;

    return true;
}

bool PrintStreamer::dequeueInFlight(
    uint32_t& lineNumber
)
{
    lineNumber = 0;

    if (inFlightCount_ == 0)
    {
        return false;
    }

    lineNumber =
        inFlightLines_[
            inFlightHead_
        ];

    inFlightLines_[
        inFlightHead_
    ] = 0;

    inFlightHead_ =
        (
            inFlightHead_ + 1
        ) %
        MAX_INFLIGHT_COMMANDS;

    --inFlightCount_;

    if (inFlightCount_ == 0)
    {
        inFlightHead_ = 0;
    }

    return true;
}

bool PrintStreamer::peekInFlight(
    uint32_t& lineNumber
) const
{
    lineNumber = 0;

    if (inFlightCount_ == 0)
    {
        return false;
    }

    lineNumber =
        inFlightLines_[
            inFlightHead_
        ];

    return true;
}

void PrintStreamer::clearInFlight()
{
    for (
        size_t index = 0;
        index < MAX_INFLIGHT_COMMANDS;
        ++index
    )
    {
        inFlightLines_[index] = 0;
    }

    inFlightHead_ = 0;
    inFlightCount_ = 0;
}

bool PrintStreamer::parseResendLineNumber(
    const char* response,
    uint32_t& lineNumber
) const
{
    lineNumber = 0;

    if (response == nullptr)
    {
        return false;
    }

    const char* position =
        nullptr;

    const size_t length =
        strlen(response);

    for (
        size_t index = 0;
        index < length;
        ++index
    )
    {
        const char* candidate =
            response + index;

        if (
            startsWithIgnoreCase(
                candidate,
                "resend:"
            )
        )
        {
            position =
                candidate +
                strlen("resend:");

            break;
        }

        if (
            startsWithIgnoreCase(
                candidate,
                "resend "
            )
        )
        {
            position =
                candidate +
                strlen("resend ");

            break;
        }

        if (
            startsWithIgnoreCase(
                candidate,
                "rs:"
            )
        )
        {
            position =
                candidate +
                strlen("rs:");

            break;
        }

        if (
            startsWithIgnoreCase(
                candidate,
                "rs "
            )
        )
        {
            position =
                candidate +
                strlen("rs ");

            break;
        }
    }

    if (position == nullptr)
    {
        return false;
    }

    position =
        skipWhitespace(
            position
        );

    if (
        position[0] == 'N' ||
        position[0] == 'n'
    )
    {
        ++position;

        position =
            skipWhitespace(
                position
            );
    }

    if (
        !isdigit(
            static_cast<unsigned char>(
                position[0]
            )
        )
    )
    {
        return false;
    }

    char* end = nullptr;

    const unsigned long value =
        strtoul(
            position,
            &end,
            10
        );

    if (
        end == position ||
        value == 0
    )
    {
        return false;
    }

    lineNumber =
        static_cast<uint32_t>(
            value
        );

    return true;
}

bool PrintStreamer::startsWithIgnoreCase(
    const char* text,
    const char* prefix
) const
{
    if (
        text == nullptr ||
        prefix == nullptr
    )
    {
        return false;
    }

    while (
        prefix[0] != '\0'
    )
    {
        if (
            text[0] == '\0'
        )
        {
            return false;
        }

        if (
            tolower(
                static_cast<unsigned char>(
                    text[0]
                )
            ) !=
            tolower(
                static_cast<unsigned char>(
                    prefix[0]
                )
            )
        )
        {
            return false;
        }

        ++text;
        ++prefix;
    }

    return true;
}

bool PrintStreamer::containsIgnoreCase(
    const char* text,
    const char* searchText
) const
{
    if (
        text == nullptr ||
        searchText == nullptr
    )
    {
        return false;
    }

    if (
        searchText[0] ==
        '\0'
    )
    {
        return true;
    }

    while (
        text[0] != '\0'
    )
    {
        if (
            startsWithIgnoreCase(
                text,
                searchText
            )
        )
        {
            return true;
        }

        ++text;
    }

    return false;
}

const char* PrintStreamer::skipWhitespace(
    const char* text
) const
{
    if (text == nullptr)
    {
        return "";
    }

    while (
        text[0] != '\0' &&
        isspace(
            static_cast<unsigned char>(
                text[0]
            )
        )
    )
    {
        ++text;
    }

    return text;
}

void PrintStreamer::updateTrackedPrinterState(
    const char* command
)
{
    updatePositioningMode(
        command
    );

    updateTrackedZPosition(
        command
    );

    updateTrackedNozzleTarget(
        command
    );

    updateTrackedBedTarget(
        command
    );
}

void PrintStreamer::updatePositioningMode(
    const char* command
)
{
    long code = -1;

    if (
        !parseCommandCode(
            command,
            'G',
            code
        )
    )
    {
        return;
    }

    if (code == 90)
    {
        positioningMode_ =
            PositioningMode::Absolute;
    }
    else if (
        code == 91
    )
    {
        positioningMode_ =
            PositioningMode::Relative;
    }
}

void PrintStreamer::updateTrackedZPosition(
    const char* command
)
{
    long code = -1;

    if (
        !parseCommandCode(
            command,
            'G',
            code
        )
    )
    {
        return;
    }

    if (code == 28)
    {
        float ignored = 0.0f;

        const bool hasX =
            parseParameter(
                command,
                'X',
                ignored
            );

        const bool hasY =
            parseParameter(
                command,
                'Y',
                ignored
            );

        const bool hasZ =
            parseParameter(
                command,
                'Z',
                ignored
            );

        if (
            hasZ ||
            (
                !hasX &&
                !hasY
            )
        )
        {
            currentZPositionMm_ =
                0.0f;

            currentZPositionKnown_ =
                true;
        }

        return;
    }

    if (
        code != 0 &&
        code != 1
    )
    {
        return;
    }

    float value = 0.0f;

    if (
        !parseParameter(
            command,
            'Z',
            value
        )
    )
    {
        return;
    }

    if (
        positioningMode_ ==
        PositioningMode::Relative
    )
    {
        if (currentZPositionKnown_)
        {
            currentZPositionMm_ +=
                value;
        }
    }
    else
    {
        currentZPositionMm_ =
            value;

        currentZPositionKnown_ =
            true;
    }

    if (
        currentZPositionKnown_ &&
        currentZPositionMm_ <
            0.0f
    )
    {
        currentZPositionMm_ =
            0.0f;
    }
}

void PrintStreamer::updateTrackedNozzleTarget(
    const char* command
)
{
    long code = -1;

    if (
        !parseCommandCode(
            command,
            'M',
            code
        )
    )
    {
        return;
    }

    if (
        code != 104 &&
        code != 109
    )
    {
        return;
    }

    float target = 0.0f;

    bool found =
        parseParameter(
            command,
            'S',
            target
        );

    if (
        !found &&
        code == 109
    )
    {
        found =
            parseParameter(
                command,
                'R',
                target
            );
    }

    if (
        !found ||
        target < 0.0f ||
        target >
            MAXIMUM_VALID_NOZZLE_TARGET_C
    )
    {
        return;
    }

    nozzleTargetC_ =
        target;

    nozzleTargetKnown_ =
        true;
}

void PrintStreamer::updateTrackedBedTarget(
    const char* command
)
{
    long code = -1;

    if (
        !parseCommandCode(
            command,
            'M',
            code
        )
    )
    {
        return;
    }

    if (
        code != 140 &&
        code != 190
    )
    {
        return;
    }

    float target = 0.0f;

    bool found =
        parseParameter(
            command,
            'S',
            target
        );

    if (
        !found &&
        code == 190
    )
    {
        found =
            parseParameter(
                command,
                'R',
                target
            );
    }

    if (
        !found ||
        target < 0.0f ||
        target >
            MAXIMUM_VALID_BED_TARGET_C
    )
    {
        return;
    }

    bedTargetC_ =
        target;

    bedTargetKnown_ =
        true;
}

void PrintStreamer::updateTemperaturesFromResponse(
    const char* response
)
{
    if (response == nullptr)
    {
        return;
    }

    float parsedNozzleTemperature =
        nozzleTemperatureC_;

    float parsedNozzleTarget =
        nozzleTargetC_;

    float parsedBedTemperature =
        bedTemperatureC_;

    float parsedBedTarget =
        bedTargetC_;

    const bool nozzleParsed =
        parseTemperatureField(
            response,
            "T:",
            parsedNozzleTemperature,
            parsedNozzleTarget
        );

    const bool bedParsed =
        parseTemperatureField(
            response,
            "B:",
            parsedBedTemperature,
            parsedBedTarget
        );

    if (nozzleParsed)
    {
        nozzleTemperatureC_ =
            parsedNozzleTemperature;

        nozzleTemperatureKnown_ =
            true;

        if (
            parsedNozzleTarget >=
                0.0f &&
            parsedNozzleTarget <=
                MAXIMUM_VALID_NOZZLE_TARGET_C
        )
        {
            nozzleTargetC_ =
                parsedNozzleTarget;

            nozzleTargetKnown_ =
                true;
        }
    }

    if (bedParsed)
    {
        bedTemperatureC_ =
            parsedBedTemperature;

        bedTemperatureKnown_ =
            true;

        if (
            parsedBedTarget >=
                0.0f &&
            parsedBedTarget <=
                MAXIMUM_VALID_BED_TARGET_C
        )
        {
            bedTargetC_ =
                parsedBedTarget;

            bedTargetKnown_ =
                true;
        }
    }

    if (
        nozzleParsed ||
        bedParsed
    )
    {
        lastTemperatureUpdateMs_ =
            millis();
    }
}

bool PrintStreamer::parseTemperatureField(
    const char* response,
    const char* fieldName,
    float& currentTemperature,
    float& targetTemperature
) const
{
    if (
        response == nullptr ||
        fieldName == nullptr ||
        fieldName[0] == '\0'
    )
    {
        return false;
    }

    const char* fieldPosition =
        strstr(
            response,
            fieldName
        );

    if (fieldPosition == nullptr)
    {
        return false;
    }

    const char* numberPosition =
        fieldPosition +
        strlen(fieldName);

    while (
        numberPosition[0] == ' ' ||
        numberPosition[0] == '\t'
    )
    {
        ++numberPosition;
    }

    char* currentEnd = nullptr;

    const float parsedCurrent =
        strtof(
            numberPosition,
            &currentEnd
        );

    if (
        currentEnd ==
        numberPosition
    )
    {
        return false;
    }

    if (
        !temperatureValueIsValid(
            parsedCurrent
        )
    )
    {
        return false;
    }

    currentTemperature =
        parsedCurrent;

    const char* slashPosition =
        currentEnd;

    while (
        slashPosition[0] == ' ' ||
        slashPosition[0] == '\t'
    )
    {
        ++slashPosition;
    }

    if (
        slashPosition[0] !=
        '/'
    )
    {
        return true;
    }

    ++slashPosition;

    while (
        slashPosition[0] == ' ' ||
        slashPosition[0] == '\t'
    )
    {
        ++slashPosition;
    }

    char* targetEnd = nullptr;

    const float parsedTarget =
        strtof(
            slashPosition,
            &targetEnd
        );

    if (
        targetEnd != slashPosition &&
        temperatureValueIsValid(
            parsedTarget
        )
    )
    {
        targetTemperature =
            parsedTarget;
    }

    return true;
}

bool PrintStreamer::temperatureValueIsValid(
    float temperature
) const
{
    if (!isfinite(temperature))
    {
        return false;
    }

    return
        temperature >=
            MINIMUM_VALID_MEASURED_TEMPERATURE_C &&
        temperature <=
            MAXIMUM_VALID_MEASURED_TEMPERATURE_C;
}

bool PrintStreamer::parseCommandCode(
    const char* command,
    char expectedLetter,
    long& commandCode
) const
{
    commandCode = -1;

    if (command == nullptr)
    {
        return false;
    }

    const char* position =
        skipWhitespace(
            command
        );

    if (
        toupper(
            static_cast<unsigned char>(
                position[0]
            )
        ) !=
        toupper(
            static_cast<unsigned char>(
                expectedLetter
            )
        )
    )
    {
        return false;
    }

    ++position;

    if (
        !isdigit(
            static_cast<unsigned char>(
                position[0]
            )
        )
    )
    {
        return false;
    }

    char* end = nullptr;

    commandCode =
        strtol(
            position,
            &end,
            10
        );

    return
        end != position;
}

bool PrintStreamer::parseParameter(
    const char* command,
    char parameterLetter,
    float& value
) const
{
    value = 0.0f;

    if (command == nullptr)
    {
        return false;
    }

    const char wanted =
        static_cast<char>(
            toupper(
                static_cast<unsigned char>(
                    parameterLetter
                )
            )
        );

    const char* position =
        command;

    while (
        position[0] != '\0'
    )
    {
        const char current =
            static_cast<char>(
                toupper(
                    static_cast<unsigned char>(
                        position[0]
                    )
                )
            );

        if (current == wanted)
        {
            const bool boundary =
                position == command ||
                isspace(
                    static_cast<unsigned char>(
                        position[-1]
                    )
                );

            if (!boundary)
            {
                ++position;

                continue;
            }

            const char* number =
                position + 1;

            while (
                number[0] == ' ' ||
                number[0] == '\t'
            )
            {
                ++number;
            }

            char* end = nullptr;

            const float parsed =
                strtof(
                    number,
                    &end
                );

            if (
                end != number
            )
            {
                value =
                    parsed;

                return true;
            }
        }

        ++position;
    }

    return false;
}

float PrintStreamer::calculateSafeLiftDistance() const
{
    if (!currentZPositionKnown_)
    {
        return
            REQUESTED_LIFT_DISTANCE_MM;
    }

    const float available =
        MAXIMUM_MACHINE_Z_MM -
        currentZPositionMm_;

    if (available <= 0.0f)
    {
        return 0.0f;
    }

    if (
        available <
        REQUESTED_LIFT_DISTANCE_MM
    )
    {
        return available;
    }

    return
        REQUESTED_LIFT_DISTANCE_MM;
}

void PrintStreamer::checkConnection()
{
    if (printer_.connected())
    {
        return;
    }

    fail(
        PrintFailureReason::PrinterDisconnected,
        "USB printer disconnected during print"
    );
}

void PrintStreamer::checkCommunicationTimeout()
{
    if (
        state_ ==
            StreamerState::Paused ||
        state_ ==
            StreamerState::SendLineNumberReset
    )
    {
        return;
    }

    if (!waitingForAcknowledgement())
    {
        return;
    }

    if (
        millis() -
            lastPrinterResponseMs_ <=
        RESPONSE_TIMEOUT_MS
    )
    {
        return;
    }

    fail(
        PrintFailureReason::CommunicationTimeout,
        "Printer stopped responding"
    );
}

void PrintStreamer::checkForCompletion()
{
    if (
        !fileExhausted_ ||
        pendingCommandValid_
    )
    {
        return;
    }

    if (inFlightCount_ > 0)
    {
        state_ =
            StreamerState::Draining;

        return;
    }

    gcodeFile_.close();

    state_ =
        StreamerState::Finished;

    printJob_.markFinished();
}

void PrintStreamer::fail(
    PrintFailureReason reason,
    const char* message
)
{
    state_ =
        StreamerState::Error;

    clearPendingCommand();
    clearInFlight();

    gcodeFile_.close();

    printJob_.markError(
        reason,
        message
    );
}

void PrintStreamer::clearPendingCommand()
{
    pendingCommandValid_ =
        false;

    pendingRawCommand_[0] =
        '\0';

    pendingFormattedCommand_[0] =
        '\0';

    pendingLineNumber_ = 0;
    pendingFileStartPosition_ = 0;
    pendingFileEndPosition_ = 0;
}

void PrintStreamer::resetRuntimeState()
{
    gcodeFile_.close();

    protocol_.reset(1);
    history_.clear();
    printJob_.reset();

    clearInFlight();
    clearPendingCommand();

    state_ =
        StreamerState::Idle;

    stateBeforeResend_ =
        StreamerState::Idle;

    positioningMode_ =
        PositioningMode::Absolute;

    fileExhausted_ =
        false;

    currentInFlightCommandIsControl_ =
        false;

    resendReturnCommandWasControl_ =
        false;

    resendNextLine_ = 0;
    resendLastLine_ = 0;

    lastPrinterResponseMs_ = 0;
    lastAcknowledgementMs_ = 0;
    lastLoopExecutionMs_ = 0;

    unexpectedOkCount_ = 0;
    transmissionFailureCount_ = 0;

    currentZPositionMm_ =
        0.0f;

    nozzleTemperatureC_ =
        0.0f;

    nozzleTargetC_ =
        0.0f;

    bedTemperatureC_ =
        0.0f;

    bedTargetC_ =
        0.0f;

    savedNozzleTargetC_ =
        0.0f;

    appliedLiftDistanceMm_ =
        0.0f;

    currentZPositionKnown_ =
        false;

    nozzleTemperatureKnown_ =
        false;

    nozzleTargetKnown_ =
        false;

    bedTemperatureKnown_ =
        false;

    bedTargetKnown_ =
        false;

    lastTemperatureUpdateMs_ =
        0;
}