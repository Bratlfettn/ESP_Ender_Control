#pragma once

#include <Arduino.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "CameraStreamService.h"
#include "EnvironmentSensor.h"
#include "PrintStreamer.h"
#include "UsbPrinter.h"
#include "GcodeUploadService.h"

class WiFiDashboard
{
public:
    WiFiDashboard(
        UsbPrinter& printer,
        PrintStreamer& printStreamer,
        CameraStreamService& cameraStream,
        EnvironmentSensor& environmentSensor,
        fs::FS& fileSystem
    );

    void begin(
        const char* ssid,
        const char* password
    );

    void loop();

    bool connected() const;
    IPAddress localIp() const;

private:
    static constexpr uint16_t HTTP_PORT = 80;

    static constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS =
        10000;

    static constexpr uint32_t WIFI_FULL_RESTART_INTERVAL_MS =
        60000;

    static constexpr const char* MDNS_HOSTNAME =
        "ender-control";

    UsbPrinter& printer_;
    PrintStreamer& printStreamer_;
    CameraStreamService& cameraStream_;
    EnvironmentSensor& environmentSensor_;
    fs::FS& fileSystem_;
    GcodeUploadService uploadService_;

    WebServer server_;

    const char* ssid_;
    const char* password_;

    bool serverStarted_;
    bool mdnsStarted_;
    bool previousWiFiConnected_;

    uint32_t lastReconnectAttemptMs_;
    uint32_t wifiDisconnectedSinceMs_;
    uint32_t lastFullWiFiRestartMs_;

    void configureRoutes();
    void processWiFi();
    void reconnectWiFi();

    void startServer();
    void startMdns();
    void stopMdns();

    void handleDashboard();
    void handleStatus();
    void handleFiles();
    void handleStart();
    void handlePause();
    void handleResume();
    void handleStop();
    void handleNotFound();

    void sendJsonMessage(
        int statusCode,
        bool success,
        const char* message
    );

    String buildStatusJson() const;
    String buildFilesJson();

    String escapeJson(const char* text) const;
    bool isGcodeFile(const char* fileName) const;
};