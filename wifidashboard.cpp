#include "WiFiDashboard.h"

#include "DashboardPage.h"
#include "Logger.h"

WiFiDashboard::WiFiDashboard(
    UsbPrinter& printer,
    PrintStreamer& printStreamer,
    CameraStreamService& cameraStream,
    EnvironmentSensor& environmentSensor,
    fs::FS& fileSystem
)
    : printer_(printer),
      printStreamer_(printStreamer),
      cameraStream_(cameraStream),
      environmentSensor_(environmentSensor),
      fileSystem_(fileSystem),
      uploadService_(),
      server_(HTTP_PORT),
      ssid_(nullptr),
      password_(nullptr),
      serverStarted_(false),
      mdnsStarted_(false),
      previousWiFiConnected_(false),
      lastReconnectAttemptMs_(0),
      wifiDisconnectedSinceMs_(0),
      lastFullWiFiRestartMs_(0)
{
}

void WiFiDashboard::begin(
    const char* ssid,
    const char* password
)
{
    ssid_ = ssid;
    password_ = password;

    configureRoutes();

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);

    WiFi.setHostname(
        MDNS_HOSTNAME
    );

    previousWiFiConnected_ = false;
    mdnsStarted_ = false;

    const uint32_t currentTime =
        millis();

    lastReconnectAttemptMs_ =
        currentTime;

    wifiDisconnectedSinceMs_ =
        currentTime;

    lastFullWiFiRestartMs_ =
        currentTime;

    WiFi.begin(
        ssid_,
        password_
    );
}

void WiFiDashboard::loop()
{
    processWiFi();

    if (
        serverStarted_ &&
        WiFi.status() == WL_CONNECTED
    )
    {
        server_.handleClient();
    }
}

bool WiFiDashboard::connected() const
{
    return
        WiFi.status() ==
        WL_CONNECTED;
}

IPAddress WiFiDashboard::localIp() const
{
    return WiFi.localIP();
}

void WiFiDashboard::configureRoutes()
{
    server_.on(
        "/",
        HTTP_GET,
        [this]()
        {
            handleDashboard();
        }
    );

    server_.on(
        "/api/status",
        HTTP_GET,
        [this]()
        {
            handleStatus();
        }
    );

    server_.on(
        "/api/files",
        HTTP_GET,
        [this]()
        {
            handleFiles();
        }
    );

    server_.on(
        "/api/start",
        HTTP_POST,
        [this]()
        {
            handleStart();
        }
    );

    server_.on(
        "/api/pause",
        HTTP_POST,
        [this]()
        {
            handlePause();
        }
    );

    server_.on(
        "/api/resume",
        HTTP_POST,
        [this]()
        {
            handleResume();
        }
    );

    server_.on(
        "/api/stop",
        HTTP_POST,
        [this]()
        {
            handleStop();
        }
    );

    server_.onNotFound(
        [this]()
        {
            handleNotFound();
        }
    );

    uploadService_.begin(
        server_,
        fileSystem_,
        printStreamer_
    );
}

void WiFiDashboard::processWiFi()
{
    const uint32_t currentTime =
        millis();

    const bool currentlyConnected =
        WiFi.status() ==
        WL_CONNECTED;

    if (
        currentlyConnected &&
        !previousWiFiConnected_
    )
    {
        previousWiFiConnected_ = true;
        wifiDisconnectedSinceMs_ = 0;

        startServer();

        stopMdns();
        startMdns();

        return;
    }

    if (
        !currentlyConnected &&
        previousWiFiConnected_
    )
    {
        previousWiFiConnected_ = false;

        wifiDisconnectedSinceMs_ =
            currentTime;

        stopMdns();
    }

    if (currentlyConnected)
    {
        if (!serverStarted_)
        {
            startServer();
        }

        if (!mdnsStarted_)
        {
            startMdns();
        }

        return;
    }

    if (wifiDisconnectedSinceMs_ == 0)
    {
        wifiDisconnectedSinceMs_ =
            currentTime;
    }

    reconnectWiFi();
}

void WiFiDashboard::reconnectWiFi()
{
    const uint32_t currentTime =
        millis();

    if (
        currentTime -
            lastReconnectAttemptMs_ <
        WIFI_RECONNECT_INTERVAL_MS
    )
    {
        return;
    }

    lastReconnectAttemptMs_ =
        currentTime;

    const bool fullRestartRequired =
        currentTime -
            lastFullWiFiRestartMs_ >=
        WIFI_FULL_RESTART_INTERVAL_MS;

    if (!fullRestartRequired)
    {
        WiFi.reconnect();
        return;
    }

    lastFullWiFiRestartMs_ =
        currentTime;

    stopMdns();

    WiFi.disconnect(
        false,
        false
    );

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    WiFi.setHostname(
        MDNS_HOSTNAME
    );

    WiFi.begin(
        ssid_,
        password_
    );
}

void WiFiDashboard::startServer()
{
    if (serverStarted_)
    {
        return;
    }

    server_.begin();
    serverStarted_ = true;
}

void WiFiDashboard::startMdns()
{
    if (mdnsStarted_)
    {
        return;
    }

    if (
        WiFi.status() !=
        WL_CONNECTED
    )
    {
        return;
    }

    const IPAddress address =
        WiFi.localIP();

    if (
        address[0] == 0 &&
        address[1] == 0 &&
        address[2] == 0 &&
        address[3] == 0
    )
    {
        return;
    }

    if (!MDNS.begin(MDNS_HOSTNAME))
    {
        mdnsStarted_ = false;
        return;
    }

    MDNS.setInstanceName(
        "Ender Control"
    );

    MDNS.addService(
        "http",
        "tcp",
        HTTP_PORT
    );

    mdnsStarted_ = true;
}

void WiFiDashboard::stopMdns()
{
    if (!mdnsStarted_)
    {
        return;
    }

    MDNS.end();
    mdnsStarted_ = false;
}

void WiFiDashboard::handleDashboard()
{
    server_.sendHeader(
        "Cache-Control",
        "no-store"
    );

    server_.send_P(
        200,
        "text/html",
        DASHBOARD_HTML
    );
}

void WiFiDashboard::handleStatus()
{
    server_.sendHeader(
        "Cache-Control",
        "no-store"
    );

    server_.send(
        200,
        "application/json",
        buildStatusJson()
    );
}

void WiFiDashboard::handleFiles()
{
    server_.sendHeader(
        "Cache-Control",
        "no-store"
    );

    server_.send(
        200,
        "application/json",
        buildFilesJson()
    );
}

void WiFiDashboard::handleStart()
{
    if (printStreamer_.active())
    {
        sendJsonMessage(
            409,
            false,
            "A print is already active"
        );

        return;
    }

    if (!printer_.connected())
    {
        sendJsonMessage(
            409,
            false,
            "Printer is disconnected"
        );

        return;
    }

    if (!server_.hasArg("file"))
    {
        sendJsonMessage(
            400,
            false,
            "No file selected"
        );

        return;
    }

    String filePath =
        server_.arg("file");

    filePath.trim();

    if (!filePath.startsWith("/"))
    {
        filePath =
            "/" + filePath;
    }

    if (
        filePath.length() < 2 ||
        filePath.indexOf("..") >= 0 ||
        !isGcodeFile(
            filePath.c_str()
        )
    )
    {
        sendJsonMessage(
            400,
            false,
            "Invalid G-code file path"
        );

        return;
    }

    File testFile =
        fileSystem_.open(
            filePath.c_str(),
            FILE_READ
        );

    if (!testFile)
    {
        sendJsonMessage(
            404,
            false,
            "G-code file could not be opened"
        );

        return;
    }

    if (testFile.isDirectory())
    {
        testFile.close();

        sendJsonMessage(
            400,
            false,
            "Selected path is a directory"
        );

        return;
    }

    testFile.close();

    if (
        !printStreamer_.start(
            fileSystem_,
            filePath.c_str()
        )
    )
    {
        sendJsonMessage(
            500,
            false,
            "Print streamer failed to start"
        );

        return;
    }

    sendJsonMessage(
        200,
        true,
        "Print started"
    );
}

void WiFiDashboard::handlePause()
{
    if (!printStreamer_.active())
    {
        sendJsonMessage(
            409,
            false,
            "No print is active"
        );

        return;
    }

    if (printStreamer_.job().isPaused())
    {
        sendJsonMessage(
            409,
            false,
            "Print is already paused"
        );

        return;
    }

    printStreamer_.pause();

    sendJsonMessage(
        200,
        true,
        "Print paused"
    );
}

void WiFiDashboard::handleResume()
{
    if (!printStreamer_.job().isPaused())
    {
        sendJsonMessage(
            409,
            false,
            "Print is not paused"
        );

        return;
    }

    printStreamer_.resume();

    sendJsonMessage(
        200,
        true,
        "Print resumed"
    );
}

void WiFiDashboard::handleStop()
{
    if (!printStreamer_.active())
    {
        sendJsonMessage(
            409,
            false,
            "No print is active"
        );

        return;
    }

    printStreamer_.stop();

    sendJsonMessage(
        200,
        true,
        "Print stopped"
    );
}

void WiFiDashboard::handleNotFound()
{
    sendJsonMessage(
        404,
        false,
        "Endpoint not found"
    );
}

void WiFiDashboard::sendJsonMessage(
    int statusCode,
    bool success,
    const char* message
)
{
    String json;

    json.reserve(160);

    json += "{\"success\":";

    if (success)
    {
        json += "true";
    }
    else
    {
        json += "false";
    }

    json += ",\"message\":\"";
    json += escapeJson(message);
    json += "\"}";

    server_.sendHeader(
        "Cache-Control",
        "no-store"
    );

    server_.send(
        statusCode,
        "application/json",
        json
    );
}

String WiFiDashboard::buildStatusJson() const
{
    const PrintJob& currentJob =
        printStreamer_.job();

    String json;

    json.reserve(1400);
    json += "{";

    json += "\"printerConnected\":";
    json += printer_.connected()
        ? "true"
        : "false";

    json += ",\"active\":";
    json += printStreamer_.active()
        ? "true"
        : "false";

    json += ",\"state\":\"";
    json += escapeJson(
        currentJob.stateName()
    );
    json += "\"";

    json += ",\"file\":\"";
    json += escapeJson(
        currentJob.filePath()
    );
    json += "\"";

    json += ",\"progress\":";
    json += String(
        currentJob.progressPercent(),
        2
    );

    json += ",\"elapsedSeconds\":";
    json += String(
        static_cast<unsigned long>(
            currentJob.elapsedTimeMs() /
            1000UL
        )
    );

    json += ",\"commandsSent\":";
    json += String(
        static_cast<unsigned long>(
            currentJob.commandsSent()
        )
    );

    json += ",\"commandsAcknowledged\":";
    json += String(
        static_cast<unsigned long>(
            currentJob.commandsAcknowledged()
        )
    );

    json += ",\"commandsResent\":";
    json += String(
        static_cast<unsigned long>(
            currentJob.commandsResent()
        )
    );

    json += ",\"inFlight\":";
    json += String(
        static_cast<unsigned int>(
            printStreamer_.inFlightCount()
        )
    );

    json += ",\"hasError\":";
    json += currentJob.hasError()
        ? "true"
        : "false";

    json += ",\"errorMessage\":\"";
    json += escapeJson(
        currentJob.errorMessage()
    );
    json += "\"";

    json += ",\"printerTemperatureValid\":";

    if (printStreamer_.hasTemperatureData())
    {
        json += "true";
    }
    else
    {
        json += "false";
    }

    json += ",\"nozzleTemperatureC\":";
    json += String(
        printStreamer_.nozzleTemperatureC(),
        1
    );

    json += ",\"nozzleTargetC\":";
    json += String(
        printStreamer_.nozzleTargetC(),
        1
    );

    json += ",\"bedTemperatureC\":";
    json += String(
        printStreamer_.bedTemperatureC(),
        1
    );

    json += ",\"bedTargetC\":";
    json += String(
        printStreamer_.bedTargetC(),
        1
    );

    json += ",\"printerTemperatureAgeMs\":";
    json += String(
        static_cast<unsigned long>(
            printStreamer_.temperatureDataAgeMs()
        )
    );

    json += ",\"environmentSensorAvailable\":";

    if (environmentSensor_.available())
    {
        json += "true";
    }
    else
    {
        json += "false";
    }

    json += ",\"environmentMeasurementValid\":";

    if (
        environmentSensor_.hasValidMeasurement()
    )
    {
        json += "true";
    }
    else
    {
        json += "false";
    }

    json += ",\"ambientTemperatureC\":";
    json += String(
        environmentSensor_.temperatureC(),
        1
    );

    json += ",\"ambientHumidityPercent\":";
    json += String(
        environmentSensor_.humidityPercent(),
        1
    );

    json += ",\"ambientPressureHpa\":";
    json += String(
        environmentSensor_.pressureHpa(),
        1
    );

    json += ",\"environmentMeasurementAgeMs\":";
    json += String(
        static_cast<unsigned long>(
            environmentSensor_.measurementAgeMs()
        )
    );

    json += ",\"environmentMeasurementCount\":";
    json += String(
        static_cast<unsigned long>(
            environmentSensor_.successfulMeasurementCount()
        )
    );

    json += ",\"environmentMeasurementFailures\":";
    json += String(
        static_cast<unsigned long>(
            environmentSensor_.failedMeasurementCount()
        )
    );

    json += ",\"cameraReady\":";

    if (cameraStream_.ready())
    {
        json += "true";
    }
    else
    {
        json += "false";
    }

    json += ",\"cameraViewerConnected\":";

    if (cameraStream_.viewerConnected())
    {
        json += "true";
    }
    else
    {
        json += "false";
    }

    json += ",\"cameraProfile\":\"";
    json += escapeJson(
        cameraStream_.profileName()
    );
    json += "\"";

    json += ",\"cameraFramesCaptured\":";
    json += String(
        static_cast<unsigned long>(
            cameraStream_.framesCaptured()
        )
    );

    json += ",\"cameraFramesTransmitted\":";
    json += String(
        static_cast<unsigned long>(
            cameraStream_.framesTransmitted()
        )
    );

    json += ",\"cameraFramesDropped\":";
    json += String(
        static_cast<unsigned long>(
            cameraStream_.framesDropped()
        )
    );

    json += ",\"cameraCaptureFailures\":";
    json += String(
        static_cast<unsigned long>(
            cameraStream_.captureFailures()
        )
    );

    json += ",\"cameraFpsTimes100\":";
    json += String(
        static_cast<unsigned long>(
            cameraStream_.currentFpsTimes100()
        )
    );

    json += ",\"cameraLastFrameAgeMs\":";
    json += String(
        static_cast<unsigned long>(
            cameraStream_.lastFrameAgeMs()
        )
    );

    json += ",\"cameraStreamUrl\":\"";
    json += "http://ender-control.local:81/stream";
    json += "\"";

    json += ",\"uploadRunning\":";

    if (uploadService_.uploadRunning())
    {
        json += "true";
    }
    else
    {
        json += "false";
    }

    json += ",\"uploadReceivedBytes\":";
    json += String(
        static_cast<unsigned long>(
            uploadService_.receivedBytes()
        )
    );

    json += ",\"lastUploadSuccessful\":";

    if (uploadService_.lastUploadSuccessful())
    {
        json += "true";
    }
    else
    {
        json += "false";
    }

    json += ",\"lastUploadedFile\":\"";
    json += escapeJson(
        uploadService_.uploadedFilePath()
    );
    json += "\"";

    json += ",\"lastUploadError\":\"";
    json += escapeJson(
        uploadService_.lastError()
    );
    json += "\"";

    json += "}";

    return json;
}

String WiFiDashboard::buildFilesJson()
{
    String json;

    json.reserve(1400);
    json += "{\"files\":[";

    File root =
        fileSystem_.open("/");

    if (!root)
    {
        json += "]}";
        return json;
    }

    if (!root.isDirectory())
    {
        root.close();

        json += "]}";
        return json;
    }

    bool firstFile = true;

    while (true)
    {
        File entry =
            root.openNextFile();

        if (!entry)
        {
            break;
        }

        if (
            !entry.isDirectory() &&
            isGcodeFile(
                entry.name()
            )
        )
        {
            String path =
                entry.name();

            if (!path.startsWith("/"))
            {
                path =
                    "/" + path;
            }

            if (!firstFile)
            {
                json += ",";
            }

            firstFile = false;

            json += "{\"path\":\"";
            json += escapeJson(
                path.c_str()
            );

            json += "\",\"size\":";
            json += String(
                static_cast<unsigned long>(
                    entry.size()
                )
            );

            json += "}";
        }

        entry.close();
    }

    root.close();

    json += "]}";

    return json;
}

String WiFiDashboard::escapeJson(
    const char* text
) const
{
    String escaped;

    if (text == nullptr)
    {
        return escaped;
    }

    while (text[0] != '\0')
    {
        const char character =
            text[0];

        if (character == '"')
        {
            escaped += "\\\"";
        }
        else if (character == '\\')
        {
            escaped += "\\\\";
        }
        else if (character == '\n')
        {
            escaped += "\\n";
        }
        else if (character == '\r')
        {
            escaped += "\\r";
        }
        else if (character == '\t')
        {
            escaped += "\\t";
        }
        else
        {
            escaped += character;
        }

        ++text;
    }

    return escaped;
}

bool WiFiDashboard::isGcodeFile(
    const char* fileName
) const
{
    if (fileName == nullptr)
    {
        return false;
    }

    String normalizedName =
        fileName;

    normalizedName.toLowerCase();

    return
        normalizedName.endsWith(".gcode") ||
        normalizedName.endsWith(".gco") ||
        normalizedName.endsWith(".gc");
}