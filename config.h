#pragma once

// -----------------------------------------------------------------------------
// General
// -----------------------------------------------------------------------------

#define SERIAL_DEBUG_BAUDRATE       115200

// -----------------------------------------------------------------------------
// Printer Communication
// -----------------------------------------------------------------------------

#define PRINTER_BAUDRATE            115200

// Maximum number of commands that may be sent to Marlin without receiving "ok"
#define MAX_INFLIGHT_COMMANDS       1

// Maximum stored command history for resend requests
#define MAX_HISTORY_LINES           256

// Maximum G-code line length
#define MAX_GCODE_LINE_LENGTH       128

// -----------------------------------------------------------------------------
// Timing
// -----------------------------------------------------------------------------

// If no printer response arrives within this time,
// communication will be considered stalled.
#define RESPONSE_TIMEOUT_MS         30000

// Send M105 if no printer traffic is seen for this duration.
#define KEEPALIVE_INTERVAL_MS       5000

// Main streamer execution interval.
#define STREAMER_INTERVAL_MS        1

// -----------------------------------------------------------------------------
// BME280 Environmental Sensor
// -----------------------------------------------------------------------------

// XIAO ESP32-S3 D4 / GPIO5
#define BME280_SDA_PIN              5

// XIAO ESP32-S3 D5 / GPIO6
#define BME280_SCL_PIN              6

// BME280 sampling interval.
//
// This uses the same interval as the printer temperature status update.
#define BME280_UPDATE_INTERVAL_MS   KEEPALIVE_INTERVAL_MS

// I2C bus frequency.
#define BME280_I2C_FREQUENCY_HZ     100000

// Primary and secondary BME280 I2C addresses.
#define BME280_PRIMARY_ADDRESS      0x76
#define BME280_SECONDARY_ADDRESS    0x77

// -----------------------------------------------------------------------------
// SD Card
// -----------------------------------------------------------------------------

#define SD_CS_PIN                   21

// -----------------------------------------------------------------------------
// USB Host
// -----------------------------------------------------------------------------

#define USB_HOST_TASK_INTERVAL_MS   1

// -----------------------------------------------------------------------------
// Debug Output
// -----------------------------------------------------------------------------

#define DEBUG_USB                   true
#define DEBUG_STREAMER              true
#define DEBUG_PROTOCOL              true
#define DEBUG_SD                    true
#define DEBUG_BME280                true