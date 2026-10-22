#include "GcodeUploadService.h"

#include "Logger.h"

#include <cctype>
#include <cstring>

GcodeUploadService::GcodeUploadService()
    : server_(nullptr),
      fileSystem_(nullptr),
      printStreamer_(nullptr),
      uploadFile_(),
      uploadRunning_(false),
      uploadComplete_(false),
      uploadSuccessful_(false),
      responseSent_(false),
      receivedBytes_(0),
      finalFilePath_{0},
      lastError_{0}
{
}

void GcodeUploadService::begin(
    WebServer& server,
    fs::FS& fileSystem,
    PrintStreamer& printStreamer
)
{
    server_ = &server;
    fileSystem_ = &fileSystem;
    printStreamer_ = &printStreamer;

    resetUploadState();

    server_->on(
        UPLOAD_ENDPOINT,
        HTTP_POST,
        [this]()
        {
            handleUploadFinished();
        },
        [this]()
        {
            handleUploadData();
        }
    );
}

bool GcodeUploadService::uploadRunning() const
{
    return uploadRunning_;
}

bool GcodeUploadService::lastUploadSuccessful() const
{
    return uploadSuccessful_;
}

uint32_t GcodeUploadService::receivedBytes() const
{
    return receivedBytes_;
}

const char* GcodeUploadService::uploadedFilePath() const
{
    return finalFilePath_;
}

const char* GcodeUploadService::lastError() const
{
    return lastError_;
}

void GcodeUploadService::handleUploadData()
{
    if (
        server_ == nullptr ||
        fileSystem_ == nullptr ||
        printStreamer_ == nullptr
    )
    {
        abortUpload(
            "Upload service is not initialized"
        );

        return;
    }

    HTTPUpload& upload =
        server_->upload();

    if (upload.status == UPLOAD_FILE_START)
    {
        resetUploadState();

        beginUpload(
            upload.filename
        );

        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (!uploadRunning_)
        {
            return;
        }

        writeUploadChunk(
            upload.buf,
            upload.currentSize
        );

        return;
    }

    if (upload.status == UPLOAD_FILE_END)
    {
        if (!uploadRunning_)
        {
            return;
        }

        finishUpload();
        return;
    }

    if (upload.status == UPLOAD_FILE_ABORTED)
    {
        abortUpload(
            "Upload was cancelled"
        );
    }
}

void GcodeUploadService::handleUploadFinished()
{
    if (responseSent_)
    {
        return;
    }

    responseSent_ = true;

    if (uploadSuccessful_)
    {
        String message;

        message.reserve(220);

        message += "Uploaded ";
        message += finalFilePath_;
        message += " successfully";

        sendJsonResult(
            200,
            true,
            message.c_str()
        );

        return;
    }

    if (lastError_[0] == '\0')
    {
        setError(
            "Upload did not complete"
        );
    }

    sendJsonResult(
        400,
        false,
        lastError_
    );
}

bool GcodeUploadService::beginUpload(
    const String& browserFileName
)
{
    if (printStreamer_->active())
    {
        abortUpload(
            "Uploads are disabled while a print is active"
        );

        return false;
    }

    if (
        !buildSafeFilePath(
            browserFileName,
            finalFilePath_,
            sizeof(finalFilePath_)
        )
    )
    {
        abortUpload(
            "Invalid G-code filename"
        );

        return false;
    }

    if (
        !hasGcodeExtension(
            finalFilePath_
        )
    )
    {
        abortUpload(
            "Only .gcode, .gco, and .gc files are accepted"
        );

        return false;
    }

    if (uploadFile_)
    {
        uploadFile_.close();
    }

    fileSystem_->remove(
        TEMP_FILE_PATH
    );

    uploadFile_ =
        fileSystem_->open(
            TEMP_FILE_PATH,
            FILE_WRITE
        );

    if (!uploadFile_)
    {
        abortUpload(
            "Could not create temporary upload file"
        );

        return false;
    }

    uploadRunning_ = true;
    uploadComplete_ = false;
    uploadSuccessful_ = false;
    receivedBytes_ = 0;
    lastError_[0] = '\0';

    Logger::infof(
        "Starting G-code upload: %s",
        finalFilePath_
    );

    return true;
}

bool GcodeUploadService::writeUploadChunk(
    const uint8_t* data,
    size_t length
)
{
    if (!uploadRunning_)
    {
        return false;
    }

    if (data == nullptr && length > 0)
    {
        abortUpload(
            "Upload contained an invalid data block"
        );

        return false;
    }

    if (
        length >
        MAX_UPLOAD_SIZE_BYTES -
            receivedBytes_
    )
    {
        abortUpload(
            "Uploaded file exceeds the 100 MB limit"
        );

        return false;
    }

    if (length == 0)
    {
        return true;
    }

    const size_t bytesWritten =
        uploadFile_.write(
            data,
            length
        );

    if (bytesWritten != length)
    {
        abortUpload(
            "Failed while writing upload to microSD"
        );

        return false;
    }

    receivedBytes_ +=
        static_cast<uint32_t>(
            bytesWritten
        );

    return true;
}

bool GcodeUploadService::finishUpload()
{
    if (!uploadRunning_)
    {
        return false;
    }

    uploadRunning_ = false;
    uploadComplete_ = true;

    if (uploadFile_)
    {
        uploadFile_.flush();
        uploadFile_.close();
    }

    if (receivedBytes_ == 0)
    {
        fileSystem_->remove(
            TEMP_FILE_PATH
        );

        setError(
            "Uploaded file was empty"
        );

        uploadSuccessful_ = false;
        return false;
    }

    if (
        fileSystem_->exists(
            finalFilePath_
        )
    )
    {
        if (
            !fileSystem_->remove(
                finalFilePath_
            )
        )
        {
            fileSystem_->remove(
                TEMP_FILE_PATH
            );

            setError(
                "Could not replace the existing G-code file"
            );

            uploadSuccessful_ = false;
            return false;
        }
    }

    if (
        !fileSystem_->rename(
            TEMP_FILE_PATH,
            finalFilePath_
        )
    )
    {
        fileSystem_->remove(
            TEMP_FILE_PATH
        );

        setError(
            "Could not finalize the uploaded file"
        );

        uploadSuccessful_ = false;
        return false;
    }

    uploadSuccessful_ = true;
    lastError_[0] = '\0';

    Logger::infof(
        "G-code upload completed: %s, %lu bytes",
        finalFilePath_,
        static_cast<unsigned long>(
            receivedBytes_
        )
    );

    return true;
}

void GcodeUploadService::abortUpload(
    const char* errorMessage
)
{
    if (uploadFile_)
    {
        uploadFile_.close();
    }

    if (fileSystem_ != nullptr)
    {
        fileSystem_->remove(
            TEMP_FILE_PATH
        );
    }

    uploadRunning_ = false;
    uploadComplete_ = true;
    uploadSuccessful_ = false;

    setError(
        errorMessage
    );

    Logger::errorf(
        "G-code upload failed: %s",
        lastError_
    );
}

bool GcodeUploadService::buildSafeFilePath(
    const String& browserFileName,
    char* output,
    size_t outputSize
) const
{
    if (
        output == nullptr ||
        outputSize < 4
    )
    {
        return false;
    }

    output[0] = '\0';

    String fileName =
        browserFileName;

    fileName.trim();

    fileName.replace(
        "\\",
        "/"
    );

    const int finalSlash =
        fileName.lastIndexOf('/');

    if (finalSlash >= 0)
    {
        fileName =
            fileName.substring(
                finalSlash + 1
            );
    }

    fileName.trim();

    if (
        fileName.length() == 0 ||
        fileName == "." ||
        fileName == ".."
    )
    {
        return false;
    }

    String safeName;

    safeName.reserve(
        fileName.length()
    );

    for (
        size_t index = 0;
        index < fileName.length();
        ++index
    )
    {
        const char character =
            fileName.charAt(index);

        const bool allowed =
            isalnum(
                static_cast<unsigned char>(
                    character
                )
            ) ||
            character == '.' ||
            character == '-' ||
            character == '_' ||
            character == ' ';

        if (allowed)
        {
            safeName +=
                character;
        }
        else
        {
            safeName +=
                '_';
        }
    }

    safeName.trim();

    if (
        safeName.length() == 0 ||
        safeName.length() + 2 >
            outputSize
    )
    {
        return false;
    }

    output[0] = '/';

    memcpy(
        output + 1,
        safeName.c_str(),
        safeName.length()
    );

    output[safeName.length() + 1] =
        '\0';

    return true;
}

bool GcodeUploadService::hasGcodeExtension(
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
        normalizedName.endsWith(
            ".gcode"
        ) ||
        normalizedName.endsWith(
            ".gco"
        ) ||
        normalizedName.endsWith(
            ".gc"
        );
}

void GcodeUploadService::resetUploadState()
{
    if (uploadFile_)
    {
        uploadFile_.close();
    }

    uploadRunning_ = false;
    uploadComplete_ = false;
    uploadSuccessful_ = false;
    responseSent_ = false;

    receivedBytes_ = 0;

    finalFilePath_[0] = '\0';
    lastError_[0] = '\0';
}

void GcodeUploadService::setError(
    const char* message
)
{
    lastError_[0] = '\0';

    if (message == nullptr)
    {
        return;
    }

    size_t length =
        strlen(message);

    if (length >= sizeof(lastError_))
    {
        length =
            sizeof(lastError_) - 1;
    }

    memcpy(
        lastError_,
        message,
        length
    );

    lastError_[length] = '\0';
}

String GcodeUploadService::escapeJson(
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

void GcodeUploadService::sendJsonResult(
    int statusCode,
    bool success,
    const char* message
)
{
    if (server_ == nullptr)
    {
        return;
    }

    String json;

    json.reserve(320);

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
    json += "\"";

    json += ",\"path\":\"";
    json += escapeJson(finalFilePath_);
    json += "\"";

    json += ",\"bytes\":";
    json += String(
        static_cast<unsigned long>(
            receivedBytes_
        )
    );

    json += "}";

    server_->sendHeader(
        "Cache-Control",
        "no-store"
    );

    server_->send(
        statusCode,
        "application/json",
        json
    );
}