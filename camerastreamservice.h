#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "esp_camera.h"
#include "esp_http_server.h"

class CameraStreamService
{
public:
    enum class VideoProfile : uint8_t
    {
        Performance,
        Balanced,
        Quality
    };

    CameraStreamService();

    bool begin();

    bool ready() const;
    bool viewerConnected() const;

    bool setProfile(VideoProfile profile);
    VideoProfile profile() const;

    uint32_t framesCaptured() const;
    uint32_t framesTransmitted() const;
    uint32_t framesDropped() const;
    uint32_t captureFailures() const;

    uint32_t currentFpsTimes100() const;
    uint32_t lastFrameAgeMs() const;

    const char* profileName() const;
    const char* streamUrlPath() const;

private:
    static constexpr uint16_t STREAM_PORT = 81;

    static constexpr uint32_t STREAM_TASK_STACK_SIZE = 8192;
    static constexpr UBaseType_t STREAM_TASK_PRIORITY = 1;
    static constexpr BaseType_t STREAM_TASK_CORE = 0;

    static constexpr uint32_t CLIENT_WRITE_TIMEOUT_MS = 2000;
    static constexpr uint32_t FPS_UPDATE_INTERVAL_MS = 2000;

    static constexpr int CAMERA_PIN_PWDN = -1;
    static constexpr int CAMERA_PIN_RESET = -1;

    static constexpr int CAMERA_PIN_XCLK = 10;
    static constexpr int CAMERA_PIN_SIOD = 40;
    static constexpr int CAMERA_PIN_SIOC = 39;

    static constexpr int CAMERA_PIN_Y9 = 48;
    static constexpr int CAMERA_PIN_Y8 = 11;
    static constexpr int CAMERA_PIN_Y7 = 12;
    static constexpr int CAMERA_PIN_Y6 = 14;
    static constexpr int CAMERA_PIN_Y5 = 16;
    static constexpr int CAMERA_PIN_Y4 = 18;
    static constexpr int CAMERA_PIN_Y3 = 17;
    static constexpr int CAMERA_PIN_Y2 = 15;

    static constexpr int CAMERA_PIN_VSYNC = 38;
    static constexpr int CAMERA_PIN_HREF = 47;
    static constexpr int CAMERA_PIN_PCLK = 13;

    static constexpr int CAMERA_XCLK_FREQUENCY_HZ = 20000000;

    static constexpr const char* STREAM_PATH = "/stream";
    static constexpr const char* STATUS_PATH = "/status";

    bool initialized_;
    bool serverStarted_;
    bool viewerConnected_;

    VideoProfile activeProfile_;

    httpd_handle_t streamServer_;

    portMUX_TYPE statisticsLock_;

    uint32_t framesCaptured_;
    uint32_t framesTransmitted_;
    uint32_t framesDropped_;
    uint32_t captureFailures_;

    uint32_t currentFpsTimes100_;
    uint32_t fpsWindowStartMs_;
    uint32_t fpsWindowFrames_;
    uint32_t lastFrameMs_;

    bool initializeCamera();
    bool startStreamServer();

    bool applyProfile(VideoProfile profile);

    framesize_t frameSizeForProfile(
        VideoProfile profile
    ) const;

    int jpegQualityForProfile(
        VideoProfile profile
    ) const;

    void recordCapturedFrame();
    void recordTransmittedFrame();
    void recordDroppedFrame();
    void recordCaptureFailure();

    void updateFps();

    static esp_err_t streamHandler(
        httpd_req_t* request
    );

    static esp_err_t statusHandler(
        httpd_req_t* request
    );

    static CameraStreamService* instance_;
};
