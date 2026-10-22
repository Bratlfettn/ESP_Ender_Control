#include "CameraStreamService.h"

#include "Logger.h"

#include <cstdio>
#include <cstring>

#include <lwip/sockets.h>

CameraStreamService* CameraStreamService::instance_ = nullptr;

namespace
{
    const char STREAM_CONTENT_TYPE[] =
        "multipart/x-mixed-replace;boundary=frame";

    const char STREAM_BOUNDARY[] =
        "\r\n--frame\r\n";

    const char STREAM_PART_HEADER[] =
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %u\r\n"
        "\r\n";

    const char STATUS_CONTENT_TYPE[] =
        "application/json";

    constexpr size_t PART_HEADER_BUFFER_SIZE = 96;
    constexpr size_t STATUS_BUFFER_SIZE = 512;
}

CameraStreamService::CameraStreamService()
    : initialized_(false),
      serverStarted_(false),
      viewerConnected_(false),
      activeProfile_(VideoProfile::Balanced),
      streamServer_(nullptr),
      statisticsLock_(),
      framesCaptured_(0),
      framesTransmitted_(0),
      framesDropped_(0),
      captureFailures_(0),
      currentFpsTimes100_(0),
      fpsWindowStartMs_(0),
      fpsWindowFrames_(0),
      lastFrameMs_(0)
{
    portMUX_INITIALIZE(
        &statisticsLock_
    );
}

bool CameraStreamService::begin()
{
    if (
        initialized_ &&
        serverStarted_
    )
    {
        return true;
    }

    if (instance_ != nullptr)
    {
        Logger::error(
            "Only one camera stream service may exist"
        );

        return false;
    }

    if (!psramFound())
    {
        Logger::error(
            "Camera streaming requires PSRAM"
        );

        return false;
    }

    framesCaptured_ = 0;
    framesTransmitted_ = 0;
    framesDropped_ = 0;
    captureFailures_ = 0;

    currentFpsTimes100_ = 0;
    fpsWindowStartMs_ = millis();
    fpsWindowFrames_ = 0;
    lastFrameMs_ = 0;

    viewerConnected_ = false;

    if (!initializeCamera())
    {
        Logger::error(
            "Camera initialization failed"
        );

        return false;
    }

    initialized_ = true;
    instance_ = this;

    if (!startStreamServer())
    {
        Logger::error(
            "Camera stream server failed to start"
        );

        instance_ = nullptr;
        initialized_ = false;

        esp_camera_deinit();

        return false;
    }

    Logger::info(
        "Camera MJPEG stream service started"
    );

    Logger::info(
        "Camera stream path: http://ender-control.local:81/stream"
    );

    return true;
}

bool CameraStreamService::ready() const
{
    return
        initialized_ &&
        serverStarted_ &&
        streamServer_ != nullptr;
}

bool CameraStreamService::viewerConnected() const
{
    bool value = false;

    portENTER_CRITICAL(
        const_cast<portMUX_TYPE*>(
            &statisticsLock_
        )
    );

    value = viewerConnected_;

    portEXIT_CRITICAL(
        const_cast<portMUX_TYPE*>(
            &statisticsLock_
        )
    );

    return value;
}

bool CameraStreamService::setProfile(
    VideoProfile profile
)
{
    if (!initialized_)
    {
        activeProfile_ = profile;
        return true;
    }

    if (viewerConnected())
    {
        Logger::warning(
            "Cannot change camera profile while a viewer is connected"
        );

        return false;
    }

    if (!applyProfile(profile))
    {
        Logger::error(
            "Failed to apply camera video profile"
        );

        return false;
    }

    activeProfile_ = profile;

    Logger::infof(
        "Camera profile changed to %s",
        profileName()
    );

    return true;
}

CameraStreamService::VideoProfile
CameraStreamService::profile() const
{
    return activeProfile_;
}

uint32_t CameraStreamService::framesCaptured() const
{
    uint32_t value = 0;

    portENTER_CRITICAL(
        const_cast<portMUX_TYPE*>(
            &statisticsLock_
        )
    );

    value = framesCaptured_;

    portEXIT_CRITICAL(
        const_cast<portMUX_TYPE*>(
            &statisticsLock_
        )
    );

    return value;
}

uint32_t CameraStreamService::framesTransmitted() const
{
    uint32_t value = 0;

    portENTER_CRITICAL(
        const_cast<portMUX_TYPE*>(
            &statisticsLock_
        )
    );

    value = framesTransmitted_;

    portEXIT_CRITICAL(
        const_cast<portMUX_TYPE*>(
            &statisticsLock_
        )
    );

    return value;
}

uint32_t CameraStreamService::framesDropped() const
{
    uint32_t value = 0;

    portENTER_CRITICAL(
        const_cast<portMUX_TYPE*>(
            &statisticsLock_
        )
    );

    value = framesDropped_;

    portEXIT_CRITICAL(
        const_cast<portMUX_TYPE*>(
            &statisticsLock_
        )
    );

    return value;
}

uint32_t CameraStreamService::captureFailures() const
{
    uint32_t value = 0;

    portENTER_CRITICAL(
        const_cast<portMUX_TYPE*>(
            &statisticsLock_
        )
    );

    value = captureFailures_;

    portEXIT_CRITICAL(
        const_cast<portMUX_TYPE*>(
            &statisticsLock_
        )
    );

    return value;
}

uint32_t CameraStreamService::currentFpsTimes100() const
{
    uint32_t value = 0;

    portENTER_CRITICAL(
        const_cast<portMUX_TYPE*>(
            &statisticsLock_
        )
    );

    value = currentFpsTimes100_;

    portEXIT_CRITICAL(
        const_cast<portMUX_TYPE*>(
            &statisticsLock_
        )
    );

    return value;
}

uint32_t CameraStreamService::lastFrameAgeMs() const
{
    uint32_t lastFrameTime = 0;

    portENTER_CRITICAL(
        const_cast<portMUX_TYPE*>(
            &statisticsLock_
        )
    );

    lastFrameTime = lastFrameMs_;

    portEXIT_CRITICAL(
        const_cast<portMUX_TYPE*>(
            &statisticsLock_
        )
    );

    if (lastFrameTime == 0)
    {
        return 0;
    }

    return millis() - lastFrameTime;
}

const char* CameraStreamService::profileName() const
{
    switch (activeProfile_)
    {
        case VideoProfile::Performance:
            return "Performance";

        case VideoProfile::Balanced:
            return "Balanced";

        case VideoProfile::Quality:
            return "Quality";

        default:
            return "Unknown";
    }
}

const char* CameraStreamService::streamUrlPath() const
{
    return STREAM_PATH;
}

bool CameraStreamService::initializeCamera()
{
    camera_config_t config = {};

    config.ledc_channel =
        LEDC_CHANNEL_0;

    config.ledc_timer =
        LEDC_TIMER_0;

    config.pin_d0 =
        CAMERA_PIN_Y2;

    config.pin_d1 =
        CAMERA_PIN_Y3;

    config.pin_d2 =
        CAMERA_PIN_Y4;

    config.pin_d3 =
        CAMERA_PIN_Y5;

    config.pin_d4 =
        CAMERA_PIN_Y6;

    config.pin_d5 =
        CAMERA_PIN_Y7;

    config.pin_d6 =
        CAMERA_PIN_Y8;

    config.pin_d7 =
        CAMERA_PIN_Y9;

    config.pin_xclk =
        CAMERA_PIN_XCLK;

    config.pin_pclk =
        CAMERA_PIN_PCLK;

    config.pin_vsync =
        CAMERA_PIN_VSYNC;

    config.pin_href =
        CAMERA_PIN_HREF;

    config.pin_sccb_sda =
        CAMERA_PIN_SIOD;

    config.pin_sccb_scl =
        CAMERA_PIN_SIOC;

    config.pin_pwdn =
        CAMERA_PIN_PWDN;

    config.pin_reset =
        CAMERA_PIN_RESET;

    config.xclk_freq_hz =
        CAMERA_XCLK_FREQUENCY_HZ;

    config.pixel_format =
        PIXFORMAT_JPEG;

    config.frame_size =
        frameSizeForProfile(
            activeProfile_
        );

    config.jpeg_quality =
        jpegQualityForProfile(
            activeProfile_
        );

    config.fb_count = 2;

    config.fb_location =
        CAMERA_FB_IN_PSRAM;

    config.grab_mode =
        CAMERA_GRAB_LATEST;

    const esp_err_t initialResult =
        esp_camera_init(
            &config
        );

    if (initialResult != ESP_OK)
    {
        Logger::errorf(
            "Camera driver initialization error: 0x%08lx",
            static_cast<unsigned long>(
                initialResult
            )
        );

        return false;
    }

    sensor_t* sensor =
        esp_camera_sensor_get();

    if (sensor == nullptr)
    {
        Logger::error(
            "Camera sensor is unavailable"
        );

        esp_camera_deinit();

        return false;
    }

    sensor->set_framesize(
        sensor,
        frameSizeForProfile(
            activeProfile_
        )
    );

    sensor->set_quality(
        sensor,
        jpegQualityForProfile(
            activeProfile_
        )
    );

    sensor->set_brightness(
        sensor,
        0
    );

    sensor->set_contrast(
        sensor,
        0
    );

    sensor->set_saturation(
        sensor,
        0
    );

    sensor->set_special_effect(
        sensor,
        0
    );

    sensor->set_whitebal(
        sensor,
        1
    );

    sensor->set_awb_gain(
        sensor,
        1
    );

    sensor->set_wb_mode(
        sensor,
        0
    );

    sensor->set_exposure_ctrl(
        sensor,
        1
    );

    sensor->set_aec2(
        sensor,
        1
    );

    sensor->set_gain_ctrl(
        sensor,
        1
    );

    sensor->set_agc_gain(
        sensor,
        0
    );

    sensor->set_gainceiling(
        sensor,
        GAINCEILING_8X
    );

    sensor->set_bpc(
        sensor,
        1
    );

    sensor->set_wpc(
        sensor,
        1
    );

    sensor->set_raw_gma(
        sensor,
        1
    );

    sensor->set_lenc(
        sensor,
        1
    );

    sensor->set_hmirror(
        sensor,
        0
    );

    sensor->set_vflip(
        sensor,
        0
    );

    sensor->set_dcw(
        sensor,
        1
    );

    sensor->set_colorbar(
        sensor,
        0
    );

    if (sensor->id.PID == OV3660_PID)
    {
        sensor->set_vflip(
            sensor,
            1
        );

        sensor->set_brightness(
            sensor,
            1
        );

        sensor->set_saturation(
            sensor,
            -2
        );
    }

#if defined(OV5640_PID)
    if (sensor->id.PID == OV5640_PID)
    {
        sensor->set_vflip(
            sensor,
            1
        );
    }
#endif

    Logger::infof(
        "Camera initialized with PID 0x%04x",
        static_cast<unsigned int>(
            sensor->id.PID
        )
    );

    Logger::infof(
        "Camera profile: %s",
        profileName()
    );

    return true;
}

bool CameraStreamService::startStreamServer()
{
    if (serverStarted_)
    {
        return true;
    }

    if (!initialized_)
    {
        return false;
    }

    httpd_config_t config =
        HTTPD_DEFAULT_CONFIG();

    config.server_port =
        STREAM_PORT;

    config.ctrl_port =
        32769;

    config.task_priority =
        STREAM_TASK_PRIORITY;

    config.stack_size =
        STREAM_TASK_STACK_SIZE;

    config.core_id =
        STREAM_TASK_CORE;

    config.max_open_sockets = 2;
    config.max_uri_handlers = 4;

    config.lru_purge_enable = true;

    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;

    const esp_err_t startResult =
        httpd_start(
            &streamServer_,
            &config
        );

    if (startResult != ESP_OK)
    {
        Logger::errorf(
            "Camera HTTP server start error: 0x%08lx",
            static_cast<unsigned long>(
                startResult
            )
        );

        streamServer_ = nullptr;
        return false;
    }

    httpd_uri_t streamUri = {};

    streamUri.uri =
        STREAM_PATH;

    streamUri.method =
        HTTP_GET;

    streamUri.handler =
        streamHandler;

    streamUri.user_ctx =
        this;

    const esp_err_t streamRegistrationResult =
        httpd_register_uri_handler(
            streamServer_,
            &streamUri
        );

    if (
        streamRegistrationResult !=
        ESP_OK
    )
    {
        Logger::error(
            "Failed to register camera stream endpoint"
        );

        httpd_stop(
            streamServer_
        );

        streamServer_ = nullptr;
        return false;
    }

    httpd_uri_t statusUri = {};

    statusUri.uri =
        STATUS_PATH;

    statusUri.method =
        HTTP_GET;

    statusUri.handler =
        statusHandler;

    statusUri.user_ctx =
        this;

    const esp_err_t statusRegistrationResult =
        httpd_register_uri_handler(
            streamServer_,
            &statusUri
        );

    if (
        statusRegistrationResult !=
        ESP_OK
    )
    {
        Logger::error(
            "Failed to register camera status endpoint"
        );

        httpd_stop(
            streamServer_
        );

        streamServer_ = nullptr;
        return false;
    }

    serverStarted_ = true;

    return true;
}

bool CameraStreamService::applyProfile(
    VideoProfile profile
)
{
    sensor_t* sensor =
        esp_camera_sensor_get();

    if (sensor == nullptr)
    {
        return false;
    }

    const framesize_t requestedFrameSize =
        frameSizeForProfile(profile);

    const int requestedQuality =
        jpegQualityForProfile(profile);

    const int frameSizeResult =
        sensor->set_framesize(
            sensor,
            requestedFrameSize
        );

    if (frameSizeResult != 0)
    {
        return false;
    }

    const int qualityResult =
        sensor->set_quality(
            sensor,
            requestedQuality
        );

    if (qualityResult != 0)
    {
        return false;
    }

    return true;
}

framesize_t CameraStreamService::frameSizeForProfile(
    VideoProfile profile
) const
{
    switch (profile)
    {
        case VideoProfile::Performance:
            return FRAMESIZE_QVGA;

        case VideoProfile::Balanced:
            return FRAMESIZE_VGA;

        case VideoProfile::Quality:
            return FRAMESIZE_SVGA;

        default:
            return FRAMESIZE_VGA;
    }
}

int CameraStreamService::jpegQualityForProfile(
    VideoProfile profile
) const
{
    switch (profile)
    {
        case VideoProfile::Performance:
            return 18;

        case VideoProfile::Balanced:
            return 16;

        case VideoProfile::Quality:
            return 14;

        default:
            return 16;
    }
}

void CameraStreamService::recordCapturedFrame()
{
    portENTER_CRITICAL(
        &statisticsLock_
    );

    ++framesCaptured_;
    ++fpsWindowFrames_;

    lastFrameMs_ =
        millis();

    portEXIT_CRITICAL(
        &statisticsLock_
    );

    updateFps();
}

void CameraStreamService::recordTransmittedFrame()
{
    portENTER_CRITICAL(
        &statisticsLock_
    );

    ++framesTransmitted_;

    portEXIT_CRITICAL(
        &statisticsLock_
    );
}

void CameraStreamService::recordDroppedFrame()
{
    portENTER_CRITICAL(
        &statisticsLock_
    );

    ++framesDropped_;

    portEXIT_CRITICAL(
        &statisticsLock_
    );
}

void CameraStreamService::recordCaptureFailure()
{
    portENTER_CRITICAL(
        &statisticsLock_
    );

    ++captureFailures_;

    portEXIT_CRITICAL(
        &statisticsLock_
    );
}

void CameraStreamService::updateFps()
{
    const uint32_t currentTime =
        millis();

    portENTER_CRITICAL(
        &statisticsLock_
    );

    if (fpsWindowStartMs_ == 0)
    {
        fpsWindowStartMs_ =
            currentTime;

        fpsWindowFrames_ = 0;

        portEXIT_CRITICAL(
            &statisticsLock_
        );

        return;
    }

    const uint32_t elapsed =
        currentTime -
        fpsWindowStartMs_;

    if (
        elapsed <
        FPS_UPDATE_INTERVAL_MS
    )
    {
        portEXIT_CRITICAL(
            &statisticsLock_
        );

        return;
    }

    currentFpsTimes100_ =
        static_cast<uint32_t>(
            (
                static_cast<uint64_t>(
                    fpsWindowFrames_
                ) *
                100000ULL
            ) /
            elapsed
        );

    fpsWindowStartMs_ =
        currentTime;

    fpsWindowFrames_ = 0;

    portEXIT_CRITICAL(
        &statisticsLock_
    );
}

esp_err_t CameraStreamService::streamHandler(
    httpd_req_t* request
)
{
    CameraStreamService* service =
        static_cast<CameraStreamService*>(
            request->user_ctx
        );

    if (service == nullptr)
    {
        return ESP_FAIL;
    }

    bool viewerAccepted = false;

    portENTER_CRITICAL(
        &service->statisticsLock_
    );

    if (!service->viewerConnected_)
    {
        service->viewerConnected_ = true;
        viewerAccepted = true;
    }

    portEXIT_CRITICAL(
        &service->statisticsLock_
    );

    if (!viewerAccepted)
    {
        httpd_resp_set_status(
            request,
            "503 Service Unavailable"
        );

        httpd_resp_set_type(
            request,
            "text/plain"
        );

        httpd_resp_send(
            request,
            "Only one camera viewer is supported",
            HTTPD_RESP_USE_STRLEN
        );

        return ESP_OK;
    }

    httpd_resp_set_type(
        request,
        STREAM_CONTENT_TYPE
    );

    httpd_resp_set_hdr(
        request,
        "Access-Control-Allow-Origin",
        "*"
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store, no-cache, must-revalidate"
    );

    httpd_resp_set_hdr(
        request,
        "Pragma",
        "no-cache"
    );

    const int socketDescriptor =
        httpd_req_to_sockfd(
            request
        );

    if (socketDescriptor >= 0)
    {
        timeval timeout = {};

        timeout.tv_sec =
            CLIENT_WRITE_TIMEOUT_MS /
            1000;

        timeout.tv_usec =
            (
                CLIENT_WRITE_TIMEOUT_MS %
                1000
            ) *
            1000;

        setsockopt(
            socketDescriptor,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &timeout,
            sizeof(timeout)
        );
    }

    esp_err_t streamResult =
        ESP_OK;

    while (
        streamResult ==
        ESP_OK
    )
    {
        camera_fb_t* frame =
            esp_camera_fb_get();

        if (frame == nullptr)
        {
            service->recordCaptureFailure();

            vTaskDelay(
                pdMS_TO_TICKS(10)
            );

            continue;
        }

        service->recordCapturedFrame();

        if (
            frame->format !=
            PIXFORMAT_JPEG
        )
        {
            service->recordDroppedFrame();

            esp_camera_fb_return(
                frame
            );

            vTaskDelay(
                pdMS_TO_TICKS(1)
            );

            continue;
        }

        char partHeader[
            PART_HEADER_BUFFER_SIZE
        ];

        const int partHeaderLength =
            snprintf(
                partHeader,
                sizeof(partHeader),
                STREAM_PART_HEADER,
                static_cast<unsigned int>(
                    frame->len
                )
            );

        if (
            partHeaderLength <= 0 ||
            static_cast<size_t>(
                partHeaderLength
            ) >= sizeof(partHeader)
        )
        {
            service->recordDroppedFrame();

            esp_camera_fb_return(
                frame
            );

            streamResult =
                ESP_FAIL;

            break;
        }

        streamResult =
            httpd_resp_send_chunk(
                request,
                STREAM_BOUNDARY,
                strlen(
                    STREAM_BOUNDARY
                )
            );

        if (
            streamResult ==
            ESP_OK
        )
        {
            streamResult =
                httpd_resp_send_chunk(
                    request,
                    partHeader,
                    static_cast<size_t>(
                        partHeaderLength
                    )
                );
        }

        if (
            streamResult ==
            ESP_OK
        )
        {
            streamResult =
                httpd_resp_send_chunk(
                    request,
                    reinterpret_cast<const char*>(
                        frame->buf
                    ),
                    frame->len
                );
        }

        if (
            streamResult ==
            ESP_OK
        )
        {
            service->recordTransmittedFrame();
        }
        else
        {
            service->recordDroppedFrame();
        }

        esp_camera_fb_return(
            frame
        );

        taskYIELD();
    }

    portENTER_CRITICAL(
        &service->statisticsLock_
    );

    service->viewerConnected_ =
        false;

    service->currentFpsTimes100_ =
        0;

    service->fpsWindowFrames_ =
        0;

    service->fpsWindowStartMs_ =
        millis();

    portEXIT_CRITICAL(
        &service->statisticsLock_
    );

    return ESP_OK;
}

esp_err_t CameraStreamService::statusHandler(
    httpd_req_t* request
)
{
    CameraStreamService* service =
        static_cast<CameraStreamService*>(
            request->user_ctx
        );

    if (service == nullptr)
    {
        return ESP_FAIL;
    }

    char response[
        STATUS_BUFFER_SIZE
    ];

    const uint32_t fpsTimes100 =
        service->currentFpsTimes100();

    const uint32_t fpsWhole =
        fpsTimes100 /
        100;

    const uint32_t fpsFraction =
        fpsTimes100 %
        100;

    const int responseLength =
        snprintf(
            response,
            sizeof(response),
            "{"
            "\"ready\":%s,"
            "\"viewerConnected\":%s,"
            "\"profile\":\"%s\","
            "\"framesCaptured\":%lu,"
            "\"framesTransmitted\":%lu,"
            "\"framesDropped\":%lu,"
            "\"captureFailures\":%lu,"
            "\"fps\":\"%lu.%02lu\","
            "\"lastFrameAgeMs\":%lu"
            "}",
            service->ready()
                ? "true"
                : "false",
            service->viewerConnected()
                ? "true"
                : "false",
            service->profileName(),
            static_cast<unsigned long>(
                service->framesCaptured()
            ),
            static_cast<unsigned long>(
                service->framesTransmitted()
            ),
            static_cast<unsigned long>(
                service->framesDropped()
            ),
            static_cast<unsigned long>(
                service->captureFailures()
            ),
            static_cast<unsigned long>(
                fpsWhole
            ),
            static_cast<unsigned long>(
                fpsFraction
            ),
            static_cast<unsigned long>(
                service->lastFrameAgeMs()
            )
        );

    if (
        responseLength < 0 ||
        static_cast<size_t>(
            responseLength
        ) >= sizeof(response)
    )
    {
        httpd_resp_set_status(
            request,
            "500 Internal Server Error"
        );

        httpd_resp_set_type(
            request,
            "text/plain"
        );

        httpd_resp_send(
            request,
            "Could not build camera status",
            HTTPD_RESP_USE_STRLEN
        );

        return ESP_OK;
    }

    httpd_resp_set_type(
        request,
        STATUS_CONTENT_TYPE
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store"
    );

    httpd_resp_set_hdr(
        request,
        "Access-Control-Allow-Origin",
        "*"
    );

    return httpd_resp_send(
        request,
        response,
        static_cast<ssize_t>(
            responseLength
        )
    );
}