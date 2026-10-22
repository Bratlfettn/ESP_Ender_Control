#pragma once

#include <Arduino.h>

class EnvironmentSensor
{
public:
    EnvironmentSensor(
        int sdaPin,
        int sclPin,
        uint32_t updateIntervalMs
    );

    ~EnvironmentSensor();

    bool begin();

    void loop();

    bool available() const;
    bool hasValidMeasurement() const;

    float temperatureC() const;
    float humidityPercent() const;
    float pressureHpa() const;

    uint8_t i2cAddress() const;

    uint32_t lastMeasurementTimeMs() const;
    uint32_t measurementAgeMs() const;
    uint32_t successfulMeasurementCount() const;
    uint32_t failedMeasurementCount() const;

private:
    class Implementation;

    Implementation* implementation_;

    int sdaPin_;
    int sclPin_;

    uint32_t updateIntervalMs_;
    uint32_t lastMeasurementTimeMs_;
    uint32_t lastMeasurementAttemptMs_;

    uint32_t successfulMeasurementCount_;
    uint32_t failedMeasurementCount_;

    bool available_;
    bool validMeasurement_;

    uint8_t i2cAddress_;

    float temperatureC_;
    float humidityPercent_;
    float pressureHpa_;

    bool initializeAtAddress(
        uint8_t address
    );

    bool readMeasurement();

    bool measurementIsValid(
        float temperatureC,
        float humidityPercent,
        float pressureHpa
    ) const;
};