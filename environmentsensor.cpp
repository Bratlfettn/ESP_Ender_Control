#include "EnvironmentSensor.h"

#include "Config.h"

#include <Wire.h>
#include <Adafruit_BME280.h>

#include <cmath>
#include <new>

#include <cmath>
#include <new>

class EnvironmentSensor::Implementation
{
public:
    Adafruit_BME280 sensor;
};

EnvironmentSensor::EnvironmentSensor(
    int sdaPin,
    int sclPin,
    uint32_t updateIntervalMs
)
    : implementation_(nullptr),
      sdaPin_(sdaPin),
      sclPin_(sclPin),
      updateIntervalMs_(updateIntervalMs),
      lastMeasurementTimeMs_(0),
      lastMeasurementAttemptMs_(0),
      successfulMeasurementCount_(0),
      failedMeasurementCount_(0),
      available_(false),
      validMeasurement_(false),
      i2cAddress_(0),
      temperatureC_(0.0f),
      humidityPercent_(0.0f),
      pressureHpa_(0.0f)
{
}

EnvironmentSensor::~EnvironmentSensor()
{
    delete implementation_;

    implementation_ = nullptr;
}

bool EnvironmentSensor::begin()
{
    available_ = false;
    validMeasurement_ = false;

    i2cAddress_ = 0;

    temperatureC_ = 0.0f;
    humidityPercent_ = 0.0f;
    pressureHpa_ = 0.0f;

    lastMeasurementTimeMs_ = 0;
    lastMeasurementAttemptMs_ = 0;

    successfulMeasurementCount_ = 0;
    failedMeasurementCount_ = 0;

    if (implementation_ == nullptr)
    {
        implementation_ =
            new (std::nothrow) Implementation();

        if (implementation_ == nullptr)
        {
            return false;
        }
    }

    Wire.begin(
        sdaPin_,
        sclPin_
    );

    Wire.setClock(
        BME280_I2C_FREQUENCY_HZ
    );

    if (
        initializeAtAddress(
            BME280_PRIMARY_ADDRESS
        )
    )
    {
        available_ = true;
    }
    else if (
        initializeAtAddress(
            BME280_SECONDARY_ADDRESS
        )
    )
    {
        available_ = true;
    }
    else
    {
        available_ = false;
        validMeasurement_ = false;

        return false;
    }

    implementation_->sensor.setSampling(
        Adafruit_BME280::MODE_FORCED,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::FILTER_OFF,
        Adafruit_BME280::STANDBY_MS_1000
    );

    return readMeasurement();
}

void EnvironmentSensor::loop()
{
    if (!available_)
    {
        return;
    }

    const uint32_t currentTime =
        millis();

    if (
        lastMeasurementAttemptMs_ != 0 &&
        currentTime -
            lastMeasurementAttemptMs_ <
            updateIntervalMs_
    )
    {
        return;
    }

    readMeasurement();
}

bool EnvironmentSensor::available() const
{
    return available_;
}

bool EnvironmentSensor::hasValidMeasurement() const
{
    return validMeasurement_;
}

float EnvironmentSensor::temperatureC() const
{
    return temperatureC_;
}

float EnvironmentSensor::humidityPercent() const
{
    return humidityPercent_;
}

float EnvironmentSensor::pressureHpa() const
{
    return pressureHpa_;
}

uint8_t EnvironmentSensor::i2cAddress() const
{
    return i2cAddress_;
}

uint32_t EnvironmentSensor::lastMeasurementTimeMs() const
{
    return lastMeasurementTimeMs_;
}

uint32_t EnvironmentSensor::measurementAgeMs() const
{
    if (
        !validMeasurement_ ||
        lastMeasurementTimeMs_ == 0
    )
    {
        return 0;
    }

    return
        millis() -
        lastMeasurementTimeMs_;
}

uint32_t EnvironmentSensor::successfulMeasurementCount() const
{
    return successfulMeasurementCount_;
}

uint32_t EnvironmentSensor::failedMeasurementCount() const
{
    return failedMeasurementCount_;
}

bool EnvironmentSensor::initializeAtAddress(
    uint8_t address
)
{
    if (implementation_ == nullptr)
    {
        return false;
    }

    if (
        !implementation_->sensor.begin(
            address,
            &Wire
        )
    )
    {
        return false;
    }

    i2cAddress_ =
        address;

    return true;
}

bool EnvironmentSensor::readMeasurement()
{
    if (
        !available_ ||
        implementation_ == nullptr
    )
    {
        return false;
    }

    lastMeasurementAttemptMs_ =
        millis();

    if (
        !implementation_->
            sensor.takeForcedMeasurement()
    )
    {
        ++failedMeasurementCount_;

        return false;
    }

    const float measuredTemperatureC =
        implementation_->
            sensor.readTemperature();

    const float measuredHumidityPercent =
        implementation_->
            sensor.readHumidity();

    const float measuredPressureHpa =
        implementation_->
            sensor.readPressure() /
        100.0f;

    if (
        !measurementIsValid(
            measuredTemperatureC,
            measuredHumidityPercent,
            measuredPressureHpa
        )
    )
    {
        ++failedMeasurementCount_;

        return false;
    }

    temperatureC_ =
        measuredTemperatureC;

    humidityPercent_ =
        measuredHumidityPercent;

    pressureHpa_ =
        measuredPressureHpa;

    validMeasurement_ = true;

    lastMeasurementTimeMs_ =
        millis();

    ++successfulMeasurementCount_;

    return true;
}

bool EnvironmentSensor::measurementIsValid(
    float temperatureC,
    float humidityPercent,
    float pressureHpa
) const
{
    if (
        !isfinite(temperatureC) ||
        !isfinite(humidityPercent) ||
        !isfinite(pressureHpa)
    )
    {
        return false;
    }

    if (
        temperatureC < -40.0f ||
        temperatureC > 85.0f
    )
    {
        return false;
    }

    if (
        humidityPercent < 0.0f ||
        humidityPercent > 100.0f
    )
    {
        return false;
    }

    if (
        pressureHpa < 300.0f ||
        pressureHpa > 1100.0f
    )
    {
        return false;
    }

    return true;
}