#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#include "CameraStreamService.h"
#include "Config.h"
#include "EnvironmentSensor.h"
#include "Logger.h"
#include "PrintStreamer.h"
#include "Secrets.h"
#include "UsbPrinter.h"
#include "WiFiDashboard.h"

namespace
{
    constexpr int SD_SCK_PIN = 7;
    constexpr int SD_MISO_PIN = 8;
    constexpr int SD_MOSI_PIN = 9;
    constexpr int SD_CS_PIN_LOCAL = 21;

    constexpr uint32_t CAMERA_START_DELAY_MS =
        1500;

    constexpr uint32_t PRINTER_START_DELAY_MS =
        4000;

    constexpr uint32_t PRINTER_RETRY_INTERVAL_MS =
        10000;
}

UsbPrinter printer;

PrintStreamer printStreamer(
    printer
);

CameraStreamService cameraStream;

EnvironmentSensor environmentSensor(
    BME280_SDA_PIN,
    BME280_SCL_PIN,
    BME280_UPDATE_INTERVAL_MS
);

WiFiDashboard dashboard(
    printer,
    printStreamer,
    cameraStream,
    environmentSensor,
    SD
);

bool sdCardReady = false;

bool cameraStartAttempted = false;
bool cameraStarted = false;

bool environmentSensorStartAttempted = false;
bool environmentSensorStarted = false;

bool printerHostStartAttempted = false;
bool printerHostStarted = false;

uint32_t startupTimeMs = 0;
uint32_t lastPrinterStartAttemptMs = 0;

void initializeSdCard();

void initializeEnvironmentSensor();

void processCameraStartup();

void initializePrinterHost();

void processPrinterHostStartup();

void setup()
{
    Logger::begin(
        SERIAL_DEBUG_BAUDRATE
    );

    startupTimeMs =
        millis();

    dashboard.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    initializeSdCard();

    initializeEnvironmentSensor();
}

void loop()
{
    if (printerHostStarted)
    {
        printStreamer.loop();
    }

    environmentSensor.loop();

    if (printerHostStarted)
    {
        printStreamer.loop();
    }

    dashboard.loop();

    if (printerHostStarted)
    {
        printStreamer.loop();
    }

    processCameraStartup();

    if (printerHostStarted)
    {
        printStreamer.loop();
    }

    processPrinterHostStartup();

    if (printerHostStarted)
    {
        printStreamer.loop();
    }

    yield();
}

void initializeSdCard()
{
    sdCardReady = false;

    SPI.begin(
        SD_SCK_PIN,
        SD_MISO_PIN,
        SD_MOSI_PIN,
        SD_CS_PIN_LOCAL
    );

    const bool mounted =
        SD.begin(
            SD_CS_PIN_LOCAL,
            SPI
        );

    if (!mounted)
    {
        Logger::warning(
            "microSD card could not be mounted"
        );

        return;
    }

    if (
        SD.cardType() ==
        CARD_NONE
    )
    {
        Logger::warning(
            "No microSD card detected"
        );

        SD.end();

        return;
    }

    sdCardReady = true;

    Logger::info(
        "microSD card initialized"
    );
}

void initializeEnvironmentSensor()
{
    if (
        environmentSensorStartAttempted ||
        environmentSensorStarted
    )
    {
        return;
    }

    environmentSensorStartAttempted = true;

    environmentSensorStarted =
        environmentSensor.begin();

    if (!environmentSensorStarted)
    {
        Logger::warning(
            "BME280 environmental sensor unavailable"
        );

        return;
    }

    Logger::infof(
        "BME280 environmental sensor started at I2C address 0x%02X",
        static_cast<unsigned int>(
            environmentSensor.i2cAddress()
        )
    );
}

void processCameraStartup()
{
    if (
        cameraStartAttempted ||
        cameraStarted
    )
    {
        return;
    }

    const uint32_t currentTime =
        millis();

    if (
        currentTime - startupTimeMs <
        CAMERA_START_DELAY_MS
    )
    {
        return;
    }

    cameraStartAttempted = true;

    cameraStarted =
        cameraStream.begin();

    if (!cameraStarted)
    {
        Logger::warning(
            "Camera stream service unavailable"
        );

        return;
    }

    Logger::info(
        "Camera stream service started"
    );
}

void initializePrinterHost()
{
    printerHostStartAttempted = true;

    lastPrinterStartAttemptMs =
        millis();

    printerHostStarted =
        printer.begin(
            PRINTER_BAUDRATE
        );

    if (!printerHostStarted)
    {
        Logger::warning(
            "USB printer host startup failed"
        );

        return;
    }

    Logger::info(
        "USB printer host started"
    );
}

void processPrinterHostStartup()
{
    if (printerHostStarted)
    {
        return;
    }

    const uint32_t currentTime =
        millis();

    if (!printerHostStartAttempted)
    {
        if (
            currentTime - startupTimeMs <
            PRINTER_START_DELAY_MS
        )
        {
            return;
        }

        initializePrinterHost();

        return;
    }

    if (
        currentTime -
            lastPrinterStartAttemptMs <
        PRINTER_RETRY_INTERVAL_MS
    )
    {
        return;
    }

    initializePrinterHost();
}