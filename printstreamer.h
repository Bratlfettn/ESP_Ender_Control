#pragma once

#include <Arduino.h>
#include <FS.h>

#include "CommandHistory.h"
#include "Config.h"
#include "GcodeFile.h"
#include "MarlinProtocol.h"
#include "PrintJob.h"
#include "UsbPrinter.h"

class PrintStreamer
{
public:
    explicit PrintStreamer(
        UsbPrinter& printer
    );

    bool start(
        fs::FS& fileSystem,
        const char* filePath
    );

    void loop();

    void pause();
    void resume();
    void stop();

    bool active() const;
    bool waitingForAcknowledgement() const;

    bool safePauseRequested() const;
    bool safelyPaused() const;
    bool resumeInProgress() const;
    bool stopInProgress() const;

    float savedNozzleTarget() const;
    float appliedLiftDistance() const;

    size_t inFlightCount() const;

    bool hasTemperatureData() const;

    float nozzleTemperatureC() const;
    float nozzleTargetC() const;

    float bedTemperatureC() const;
    float bedTargetC() const;

    uint32_t lastTemperatureUpdateMs() const;
    uint32_t temperatureDataAgeMs() const;

    const PrintJob& job() const;
    PrintJob& job();

private:
    enum class StreamerState : uint8_t
    {
        Idle,

        SendLineNumberReset,
        WaitForLineNumberReset,

        Streaming,

        PauseRequested,
        PauseWaitForMotion,
        PauseSetRelativeMode,
        PauseLiftZ,
        PauseRestorePositioningMode,
        PauseDisableNozzle,
        Paused,

        ResumeHeatNozzle,
        ResumeSetRelativeMode,
        ResumeLowerZ,
        ResumeRestorePositioningMode,

        StopRequested,
        StopWaitForMotion,
        StopSetRelativeMode,
        StopLiftZ,
        StopRestorePositioningMode,
        StopDisableNozzle,
        StopDisableBed,
        StopDisableFan,
        StopDisableMotors,

        Resending,
        Draining,

        Finished,
        Stopped,
        Error
    };

    enum class PositioningMode : uint8_t
    {
        Absolute,
        Relative
    };

    static constexpr float REQUESTED_LIFT_DISTANCE_MM =
        10.0f;

    static constexpr float MAXIMUM_MACHINE_Z_MM =
        270.0f;

    static constexpr float MINIMUM_VALID_NOZZLE_TARGET_C =
        1.0f;

    static constexpr float MAXIMUM_VALID_NOZZLE_TARGET_C =
        300.0f;

    static constexpr float MAXIMUM_VALID_BED_TARGET_C =
        150.0f;

    static constexpr float MINIMUM_VALID_MEASURED_TEMPERATURE_C =
        -20.0f;

    static constexpr float MAXIMUM_VALID_MEASURED_TEMPERATURE_C =
        350.0f;

    static constexpr uint32_t MAX_CONTROL_TRANSMISSION_FAILURES =
        10;

    UsbPrinter& printer_;

    GcodeFile gcodeFile_;
    MarlinProtocol protocol_;
    CommandHistory history_;
    PrintJob printJob_;

    StreamerState state_;
    StreamerState stateBeforeResend_;

    PositioningMode positioningMode_;

    uint32_t inFlightLines_[MAX_INFLIGHT_COMMANDS];

    size_t inFlightHead_;
    size_t inFlightCount_;

    bool pendingCommandValid_;
    bool fileExhausted_;

    bool currentInFlightCommandIsControl_;
    bool resendReturnCommandWasControl_;

    char pendingRawCommand_[MAX_GCODE_LINE_LENGTH];
    char pendingFormattedCommand_[MAX_GCODE_LINE_LENGTH];

    uint32_t pendingLineNumber_;
    uint32_t pendingFileStartPosition_;
    uint32_t pendingFileEndPosition_;

    uint32_t resendNextLine_;
    uint32_t resendLastLine_;

    uint32_t lastPrinterResponseMs_;
    uint32_t lastAcknowledgementMs_;
    uint32_t lastLoopExecutionMs_;

    uint32_t unexpectedOkCount_;
    uint32_t transmissionFailureCount_;

    float currentZPositionMm_;

    float nozzleTemperatureC_;
    float nozzleTargetC_;

    float bedTemperatureC_;
    float bedTargetC_;

    float savedNozzleTargetC_;
    float appliedLiftDistanceMm_;

    bool currentZPositionKnown_;

    bool nozzleTemperatureKnown_;
    bool nozzleTargetKnown_;

    bool bedTemperatureKnown_;
    bool bedTargetKnown_;

    uint32_t lastTemperatureUpdateMs_;

    void processPrinterInput();

    void processPrinterResponse(
        const char* response
    );

    void processOkResponse(
        const char* response
    );

    void processBusyResponse(
        const char* response
    );

    void processResendResponse(
        const char* response
    );

    void processErrorResponse(
        const char* response
    );

    void sendLineNumberReset();

    void fillTransmissionWindow();

    void prepareNextCommand();

    void sendPendingCommand();

    void processPauseRequest();

    void processPauseSequence();

    void processResumeSequence();

    void processStopRequest();

    void processStopSequence();

    bool sendControlCommand(
        const char* command
    );

    void advanceControlSequenceAfterAcknowledgement();

    void enterPausedState();

    void finishResumeSequence();

    void finishStopSequence();

    void sendNextResendCommand();

    void finishResendSequence();

    bool enqueueInFlight(
        uint32_t lineNumber
    );

    bool dequeueInFlight(
        uint32_t& lineNumber
    );

    bool peekInFlight(
        uint32_t& lineNumber
    ) const;

    void clearInFlight();

    bool parseResendLineNumber(
        const char* response,
        uint32_t& lineNumber
    ) const;

    bool startsWithIgnoreCase(
        const char* text,
        const char* prefix
    ) const;

    bool containsIgnoreCase(
        const char* text,
        const char* searchText
    ) const;

    const char* skipWhitespace(
        const char* text
    ) const;

    void updateTrackedPrinterState(
        const char* command
    );

    void updatePositioningMode(
        const char* command
    );

    void updateTrackedZPosition(
        const char* command
    );

    void updateTrackedNozzleTarget(
        const char* command
    );

    void updateTrackedBedTarget(
        const char* command
    );

    void updateTemperaturesFromResponse(
        const char* response
    );

    bool parseTemperatureField(
        const char* response,
        const char* fieldName,
        float& currentTemperature,
        float& targetTemperature
    ) const;

    bool temperatureValueIsValid(
        float temperature
    ) const;

    bool parseCommandCode(
        const char* command,
        char expectedLetter,
        long& commandCode
    ) const;

    bool parseParameter(
        const char* command,
        char parameterLetter,
        float& value
    ) const;

    float calculateSafeLiftDistance() const;

    void checkConnection();

    void checkCommunicationTimeout();

    void checkForCompletion();

    void fail(
        PrintFailureReason reason,
        const char* message
    );

    void clearPendingCommand();

    void resetRuntimeState();
};