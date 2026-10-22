#pragma once

#include <Arduino.h>
#include <FS.h>
#include <WebServer.h>

#include "PrintStreamer.h"

class GcodeUploadService
{
public:
    GcodeUploadService();

    void begin(
        WebServer& server,
        fs::FS& fileSystem,
        PrintStreamer& printStreamer
    );

    bool uploadRunning() const;
    bool lastUploadSuccessful() const;

    uint32_t receivedBytes() const;

    const char* uploadedFilePath() const;
    const char* lastError() const;

private:
    static constexpr uint32_t MAX_UPLOAD_SIZE_BYTES =
        100UL * 1024UL * 1024UL;

    static constexpr size_t PATH_BUFFER_SIZE = 128;
    static constexpr size_t ERROR_BUFFER_SIZE = 160;

    static constexpr const char* UPLOAD_ENDPOINT =
        "/api/upload";

    static constexpr const char* TEMP_FILE_PATH =
        "/.gcode-upload.tmp";

    WebServer* server_;
    fs::FS* fileSystem_;
    PrintStreamer* printStreamer_;

    File uploadFile_;

    bool uploadRunning_;
    bool uploadComplete_;
    bool uploadSuccessful_;
    bool responseSent_;

    uint32_t receivedBytes_;

    char finalFilePath_[PATH_BUFFER_SIZE];
    char lastError_[ERROR_BUFFER_SIZE];

    void handleUploadData();
    void handleUploadFinished();

    bool beginUpload(const String& browserFileName);

    bool writeUploadChunk(
        const uint8_t* data,
        size_t length
    );

    bool finishUpload();
    void abortUpload(const char* errorMessage);

    bool buildSafeFilePath(
        const String& browserFileName,
        char* output,
        size_t outputSize
    ) const;

    bool hasGcodeExtension(
        const char* fileName
    ) const;

    void resetUploadState();

    void setError(const char* message);

    String escapeJson(const char* text) const;

    void sendJsonResult(
        int statusCode,
        bool success,
        const char* message
    );
};