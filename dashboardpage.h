#pragma once

#include <Arduino.h>

const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta
        name="viewport"
        content="width=device-width, initial-scale=1.0"
    >
    <title>Ender Control</title>

    <style>
        body {
            margin: 0;
            background: #10141c;
            color: #eef2f7;
            font-family: Arial, sans-serif;
        }

        main {
            max-width: 900px;
            margin: auto;
            padding: 20px;
        }

        h1 {
            margin: 0 0 6px 0;
        }

        .subtitle {
            margin-bottom: 20px;
            color: #9ca8ba;
        }

        .grid {
            display: grid;
            grid-template-columns: repeat(
                auto-fit,
                minmax(270px, 1fr)
            );
            gap: 14px;
        }

        .card {
            padding: 16px;
            border: 1px solid #303949;
            border-radius: 12px;
            background: #1a202b;
        }

        .wide {
            grid-column: 1 / -1;
        }

        .row {
            display: flex;
            justify-content: space-between;
            gap: 15px;
            padding: 7px 0;
            border-bottom: 1px solid #303949;
        }

        .row:last-child {
            border-bottom: none;
        }

        .value {
            max-width: 65%;
            text-align: right;
            font-weight: bold;
            overflow-wrap: anywhere;
        }

        .good {
            color: #48d477;
        }

        .bad {
            color: #ff6b6b;
        }

        .progress {
            height: 18px;
            overflow: hidden;
            border-radius: 9px;
            background: #303949;
        }

        #progressFill {
            width: 0;
            height: 100%;
            background: #28a745;
        }

        select,
        button {
            box-sizing: border-box;
            width: 100%;
            min-height: 42px;
            margin-top: 8px;
            border-radius: 8px;
            font-size: 15px;
        }

        select {
            padding: 0 9px;
            border: 1px solid #465266;
            color: #eef2f7;
            background: #10141c;
        }

        button {
            border: 0;
            color: white;
            background: #2878df;
            cursor: pointer;
        }

        button:disabled {
            cursor: not-allowed;
            opacity: 0.45;
        }

        .start {
            background: #218838;
        }

        .pause {
            background: #d48700;
        }

        .stop {
            background: #bd3030;
        }

        .buttons {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 8px;
        }

        .camera-box {
            display: flex;
            min-height: 240px;
            margin-top: 12px;
            align-items: center;
            justify-content: center;
            overflow: hidden;
            border-radius: 10px;
            background: #05070b;
        }

        #cameraImage {
            display: none;
            width: 100%;
            max-height: 520px;
            object-fit: contain;
        }

        #cameraMessage {
            padding: 20px;
            color: #9ca8ba;
            text-align: center;
        }

        #message {
            display: none;
            margin-top: 10px;
            padding: 10px;
            border-radius: 8px;
            background: #344155;
        }

        #message.error {
            background: #702525;
        }
    </style>
</head>

<body>
    <main>
        <h1>Ender Control</h1>

        <div class="subtitle">
            ESP32-S3 USB print host
        </div>

        <div class="grid">
            <section class="card">
                <h2>Printer</h2>

                <div class="row">
                    <span>USB printer</span>
                    <span id="printerStatus" class="value">
                        Loading
                    </span>
                </div>

                <div class="row">
                    <span>Print state</span>
                    <span id="printState" class="value">
                        Loading
                    </span>
                </div>

                <div class="row">
                    <span>Current file</span>
                    <span id="currentFile" class="value">
                        None
                    </span>
                </div>

                <div class="row">
                    <span>Elapsed</span>
                    <span id="elapsedTime" class="value">
                        0 seconds
                    </span>
                </div>
            </section>

            <section class="card">
                <h2>Temperatures</h2>

                <div class="row">
                    <span>Nozzle</span>
                    <span id="nozzleTemperature" class="value">
                        No data
                    </span>
                </div>

                <div class="row">
                    <span>Print bed</span>
                    <span id="bedTemperature" class="value">
                        No data
                    </span>
                </div>

                <div class="row">
                    <span>Surroundings</span>
                    <span id="ambientTemperature" class="value">
                        No data
                    </span>
                </div>

                <div class="row">
                    <span>Humidity</span>
                    <span id="ambientHumidity" class="value">
                        No data
                    </span>
                </div>

                <div class="row">
                    <span>Environment sensor</span>
                    <span id="environmentSensorStatus" class="value">
                        Loading
                    </span>
                </div>
            </section>

            <section class="card">
                <h2>Progress</h2>

                <div class="progress">
                    <div id="progressFill"></div>
                </div>

                <p id="progressText">
                    0.00 percent
                </p>

                <div class="row">
                    <span>Sent</span>
                    <span id="commandsSent" class="value">
                        0
                    </span>
                </div>

                <div class="row">
                    <span>Acknowledged</span>
                    <span
                        id="commandsAcknowledged"
                        class="value"
                    >
                        0
                    </span>
                </div>

                <div class="row">
                    <span>Resent</span>
                    <span id="commandsResent" class="value">
                        0
                    </span>
                </div>

                <div class="row">
                    <span>In flight</span>
                    <span id="inFlight" class="value">
                        0
                    </span>
                </div>
            </section>

            <section class="card">
                <h2>Start print</h2>

                <select id="fileSelect">
                    <option value="">
                        Loading files
                    </option>
                </select>

                <button
                    id="startButton"
                    class="start"
                    onclick="startPrint()"
                >
                    Start selected file
                </button>
            </section>

            <section class="card">
                <h2>Controls</h2>

                <div class="buttons">
                    <button
                        id="pauseButton"
                        class="pause"
                        onclick="sendAction('/api/pause')"
                    >
                        Pause
                    </button>

                    <button
                        id="resumeButton"
                        onclick="sendAction('/api/resume')"
                    >
                        Resume
                    </button>

                    <button
                        id="stopButton"
                        class="stop"
                        onclick="stopPrint()"
                    >
                        Stop
                    </button>

                    <button onclick="loadFiles()">
                        Refresh files
                    </button>
                </div>

                <div id="message"></div>
            </section>

            <section class="card">
                <h2>Upload G-code</h2>

                <input
                    id="uploadFile"
                    type="file"
                    accept=".gcode,.gco,.gc"
                >

                <button
                    id="uploadButton"
                    onclick="uploadGcode()"
                >
                    Upload to microSD
                </button>

                <div
                    id="uploadProgressContainer"
                    style="display:none; margin-top:12px;"
                >
                    <div class="progress">
                        <div
                            id="uploadProgressFill"
                            style="
                                width:0%;
                                height:100%;
                                background:#2563eb;
                            "
                        ></div>
                    </div>

                    <p id="uploadProgressText">
                        0 percent
                    </p>
                </div>
            </section>

            <section class="card wide">
                <h2>Printer camera</h2>

                <div class="row">
                    <span>Camera status</span>
                    <span id="cameraStatus" class="value">
                        Loading
                    </span>
                </div>

                <div class="row">
                    <span>Captured frames</span>
                    <span id="cameraCaptures" class="value">
                        0
                    </span>
                </div>

                <div class="row">
                    <span>Capture failures</span>
                    <span id="cameraFailures" class="value">
                        0
                    </span>
                </div>

                <div class="row">
                    <span>Frame age</span>
                    <span id="cameraAge" class="value">
                        No frame
                    </span>
                </div>

                <div class="camera-box">
                    <div id="cameraMessage">
                        Waiting for camera
                    </div>

                    <img
                        id="cameraImage"
                        alt="Printer camera"
                    >
                </div>
            </section>
        </div>
    </main>

    <script>
        let requestRunning = false;
        let shownError = '';

        function element(id) {
            return document.getElementById(id);
        }

        function text(id, value) {
            const target = element(id);

            if (target) {
                target.textContent = value;
            }
        }

        function showMessage(message, error) {
            const target = element('message');

            target.textContent = message;
            target.className = error ? 'error' : '';
            target.style.display = 'block';

            window.setTimeout(
                function() {
                    target.style.display = 'none';
                },
                4500
            );
        }

        function formatTemperature(
            currentTemperature,
            targetTemperature
        ) {
            const current =
                Number(currentTemperature);

            const target =
                Number(targetTemperature);

            if (!Number.isFinite(current)) {
                return 'No data';
            }

            if (
                Number.isFinite(target) &&
                target > 0
            ) {
                return (
                    current.toFixed(1) +
                    ' / ' +
                    target.toFixed(1) +
                    ' °C'
                );
            }

            return (
                current.toFixed(1) +
                ' °C'
            );
        }

        function formatTime(totalSeconds) {
            const total = Number(totalSeconds) || 0;
            const hours = Math.floor(total / 3600);
            const minutes = Math.floor((total % 3600) / 60);
            const seconds = total % 60;

            if (hours > 0) {
                return (
                    hours +
                    ' h ' +
                    minutes +
                    ' min ' +
                    seconds +
                    ' s'
                );
            }

            if (minutes > 0) {
                return (
                    minutes +
                    ' min ' +
                    seconds +
                    ' s'
                );
            }

            return seconds + ' s';
        }

        function updateCamera(status) {
          const image =
              document.getElementById(
                  'cameraImage'
              );

          const message =
              document.getElementById(
                  'cameraMessage'
              );

          const state =
              document.getElementById(
                  'cameraStatus'
              );

          const ready =
              Boolean(status.cameraReady);

          const viewerConnected =
              Boolean(
                  status.cameraViewerConnected
              );

          if (!ready) {
              state.textContent =
                  'Unavailable';

              state.className =
                  'value bad';

              image.style.display =
                  'none';

              image.removeAttribute('src');

              message.style.display =
                  'block';

              message.textContent =
                  'Camera unavailable';

              return;
          }

          state.textContent =
              viewerConnected
                  ? 'Streaming'
                  : 'Ready';

          state.className =
              'value good';

          message.style.display =
              'none';

          image.style.display =
              'block';

          if (!image.src) {
              image.src =
                  'http://' +
                  window.location.hostname +
                  ':81/stream';
          }

          document.getElementById(
              'cameraCaptures'
          ).textContent =
              status.cameraFramesCaptured || 0;

          document.getElementById(
              'cameraFailures'
          ).textContent =
              status.cameraCaptureFailures || 0;

          const fpsValue =
              Number(
                  status.cameraFpsTimes100
              ) / 100;

          const ageValue =
              Number(
                  status.cameraLastFrameAgeMs
              ) || 0;

          const ageElement =
              document.getElementById(
                  'cameraAge'
              );

          if (ageElement) {
              ageElement.textContent =
                  ageValue > 0
                      ? ageValue + ' ms'
                      : 'Waiting';
          }

          const fpsElement =
              document.getElementById(
                  'cameraFps'
              );

          if (fpsElement) {
              fpsElement.textContent =
                  fpsValue.toFixed(2) +
                  ' FPS';
          }

          const transmittedElement =
              document.getElementById(
                  'cameraTransmitted'
              );

          if (transmittedElement) {
              transmittedElement.textContent =
                  status.cameraFramesTransmitted ||
                  0;
          }

          const droppedElement =
              document.getElementById(
                  'cameraDropped'
              );

          if (droppedElement) {
              droppedElement.textContent =
                  status.cameraFramesDropped ||
                  0;
          }

          const profileElement =
              document.getElementById(
                  'cameraProfile'
              );

          if (profileElement) {
              profileElement.textContent =
                  status.cameraProfile ||
                  'Unknown';
          }
      }

        function applyStatus(status) {
            const printer = element('printerStatus');

            printer.textContent =
                status.printerConnected
                    ? 'Connected'
                    : 'Disconnected';

            printer.className =
                status.printerConnected
                    ? 'value good'
                    : 'value bad';

            text(
                'printState',
                status.state || 'Unknown'
            );

            text(
                'currentFile',
                status.file || 'None'
            );

            text(
                'elapsedTime',
                formatTime(status.elapsedSeconds)
            );

            if (status.printerTemperatureValid) {
                text(
                    'nozzleTemperature',
                    formatTemperature(
                        status.nozzleTemperatureC,
                        status.nozzleTargetC
                    )
                );

                text(
                    'bedTemperature',
                    formatTemperature(
                        status.bedTemperatureC,
                        status.bedTargetC
                    )
                );
            } else {
                text(
                    'nozzleTemperature',
                    'No data'
                );

                text(
                    'bedTemperature',
                    'No data'
                );
            }

            const environmentStatus =
                element(
                    'environmentSensorStatus'
                );

            if (
                status.environmentSensorAvailable &&
                status.environmentMeasurementValid
            ) {
                const ambientTemperature =
                    Number(
                        status.ambientTemperatureC
                    );

                const ambientHumidity =
                    Number(
                        status.ambientHumidityPercent
                    );

                if (
                    Number.isFinite(
                        ambientTemperature
                    )
                ) {
                    text(
                        'ambientTemperature',
                        ambientTemperature.toFixed(1) +
                        ' °C'
                    );
                } else {
                    text(
                        'ambientTemperature',
                        'No data'
                    );
                }

                if (
                    Number.isFinite(
                        ambientHumidity
                    )
                ) {
                    text(
                        'ambientHumidity',
                        ambientHumidity.toFixed(1) +
                        ' % RH'
                    );
                } else {
                    text(
                        'ambientHumidity',
                        'No data'
                    );
                }

                const measurementAge =
                    Number(
                        status.environmentMeasurementAgeMs
                    ) || 0;

                const measurementIsStale =
                    measurementAge > 15000;

                if (measurementIsStale) {
                    environmentStatus.textContent =
                        'Measurement outdated';

                    environmentStatus.className =
                        'value bad';
                } else {
                    environmentStatus.textContent =
                        'Available';

                    environmentStatus.className =
                        'value good';
                }
            } else if (
                status.environmentSensorAvailable
            ) {
                text(
                    'ambientTemperature',
                    'Waiting'
                );

                text(
                    'ambientHumidity',
                    'Waiting'
                );

                environmentStatus.textContent =
                    'Waiting for measurement';

                environmentStatus.className =
                    'value';
            } else {
                text(
                    'ambientTemperature',
                    'Unavailable'
                );

                text(
                    'ambientHumidity',
                    'Unavailable'
                );

                environmentStatus.textContent =
                    'Unavailable';

                environmentStatus.className =
                    'value bad';
            }

            text(
                'commandsSent',
                status.commandsSent || 0
            );

            text(
                'commandsAcknowledged',
                status.commandsAcknowledged || 0
            );

            text(
                'commandsResent',
                status.commandsResent || 0
            );

            text(
                'inFlight',
                status.inFlight || 0
            );

            const progress = Math.max(
                0,
                Math.min(
                    100,
                    Number(status.progress) || 0
                )
            );

            element('progressFill').style.width =
                progress + '%';

            text(
                'progressText',
                progress.toFixed(2) +
                ' percent'
            );

            const paused =
                status.state === 'Paused';

            element('startButton').disabled =
                status.active ||
                !status.printerConnected;

            element('pauseButton').disabled =
                !status.active ||
                paused;

            element('resumeButton').disabled =
                !paused;

            element('stopButton').disabled =
                !status.active;
            
            const uploadButton =
                document.getElementById(
                    'uploadButton'
                );

            const uploadInput =
                document.getElementById(
                    'uploadFile'
                );

            if (uploadButton) {
                uploadButton.disabled =
                    Boolean(
                        status.active ||
                        status.uploadRunning ||
                        requestRunning
                    );
            }

            if (uploadInput) {
                uploadInput.disabled =
                    Boolean(
                        status.active ||
                        status.uploadRunning ||
                        requestRunning
                    );
            }

            updateCamera(status);

            if (
                status.hasError &&
                status.errorMessage &&
                status.errorMessage !== shownError
            ) {
                shownError =
                    status.errorMessage;

                showMessage(
                    status.errorMessage,
                    true
                );
            }
            
        }

        async function updateStatus() {
            try {
                const response = await fetch(
                    '/api/status',
                    {
                        cache: 'no-store'
                    }
                );

                if (!response.ok) {
                    throw new Error(
                        'Status request failed'
                    );
                }

                applyStatus(
                    await response.json()
                );
            } catch (error) {
                text(
                    'printerStatus',
                    'Dashboard offline'
                );

                element(
                    'printerStatus'
                ).className =
                    'value bad';
            }
        }

        async function loadFiles() {
            const select =
                element('fileSelect');

            select.innerHTML =
                '<option value="">' +
                'Loading files' +
                '</option>';

            try {
                const response = await fetch(
                    '/api/files',
                    {
                        cache: 'no-store'
                    }
                );

                if (!response.ok) {
                    throw new Error(
                        'File request failed'
                    );
                }

                const result =
                    await response.json();

                select.innerHTML = '';

                if (
                    !result.files ||
                    result.files.length === 0
                ) {
                    select.innerHTML =
                        '<option value="">' +
                        'No G-code files found' +
                        '</option>';

                    return;
                }

                for (const file of result.files) {
                    const option =
                        document.createElement(
                            'option'
                        );

                    option.value = file.path;

                    option.textContent =
                        file.path +
                        ' (' +
                        file.size +
                        ' bytes)';

                    select.appendChild(option);
                }
            } catch (error) {
                select.innerHTML =
                    '<option value="">' +
                    'File list failed' +
                    '</option>';

                showMessage(
                    'Could not load G-code files',
                    true
                );
            }
        }

        async function startPrint() {
            if (requestRunning) {
                return;
            }

            const filePath =
                element('fileSelect').value;

            if (!filePath) {
                showMessage(
                    'Select a G-code file first',
                    true
                );

                return;
            }

            requestRunning = true;

            try {
                const body =
                    new URLSearchParams();

                body.append(
                    'file',
                    filePath
                );

                const response = await fetch(
                    '/api/start',
                    {
                        method: 'POST',
                        headers: {
                            'Content-Type':
                                'application/x-www-form-urlencoded'
                        },
                        body: body.toString()
                    }
                );

                const result =
                    await response.json();

                showMessage(
                    result.message,
                    !result.success
                );
            } catch (error) {
                showMessage(
                    'Unable to start print',
                    true
                );
            }

            requestRunning = false;

            updateStatus();
        }

        async function sendAction(path) {
            if (requestRunning) {
                return;
            }

            requestRunning = true;

            try {
                const response = await fetch(
                    path,
                    {
                        method: 'POST'
                    }
                );

                const result =
                    await response.json();

                showMessage(
                    result.message,
                    !result.success
                );
            } catch (error) {
                showMessage(
                    'Control request failed',
                    true
                );
            }

            requestRunning = false;

            updateStatus();
        }

        function stopPrint() {
            if (
                window.confirm(
                    'Stop the active print?'
                )
            ) {
                sendAction('/api/stop');
            }
        }

        function uploadGcode() {
            if (requestRunning) {
                return;
            }

            const input =
                document.getElementById(
                    'uploadFile'
                );

            if (
                !input ||
                !input.files ||
                input.files.length === 0
            ) {
                showMessage(
                    'Select a G-code file first',
                    true
                );

                return;
            }

            const file =
                input.files[0];

            const lowerName =
                file.name.toLowerCase();

            const validExtension =
                lowerName.endsWith('.gcode') ||
                lowerName.endsWith('.gco') ||
                lowerName.endsWith('.gc');

            if (!validExtension) {
                showMessage(
                    'Only .gcode, .gco, and .gc files may be uploaded',
                    true
                );

                return;
            }

            const formData =
                new FormData();

            formData.append(
                'file',
                file,
                file.name
            );

            const request =
                new XMLHttpRequest();

            const button =
                document.getElementById(
                    'uploadButton'
                );

            const progressContainer =
                document.getElementById(
                    'uploadProgressContainer'
                );

            const progressFill =
                document.getElementById(
                    'uploadProgressFill'
                );

            const progressText =
                document.getElementById(
                    'uploadProgressText'
                );

            requestRunning = true;

            button.disabled = true;

            progressContainer.style.display =
                'block';

            progressFill.style.width =
                '0%';

            progressText.textContent =
                'Preparing upload';

            request.upload.onprogress =
                function(event) {
                    if (!event.lengthComputable) {
                        progressText.textContent =
                            'Uploading';

                        return;
                    }

                    const percent =
                        Math.round(
                            event.loaded *
                            100 /
                            event.total
                        );

                    progressFill.style.width =
                        percent + '%';

                    progressText.textContent =
                        percent + ' percent';
                };

            request.onload =
                async function() {
                    requestRunning = false;
                    button.disabled = false;

                    let result = null;

                    try {
                        result =
                            JSON.parse(
                                request.responseText
                            );
                    } catch (error) {
                        showMessage(
                            'Upload returned an invalid response',
                            true
                        );

                        progressText.textContent =
                            'Upload failed';

                        return;
                    }

                    showMessage(
                        result.message ||
                        (
                            result.success
                                ? 'Upload completed'
                                : 'Upload failed'
                        ),
                        !result.success
                    );

                    if (!result.success) {
                        progressText.textContent =
                            'Upload failed';

                        return;
                    }

                    progressFill.style.width =
                        '100%';

                    progressText.textContent =
                        'Upload complete';

                    input.value = '';

                    await loadFiles();
                    await updateStatus();
                };

            request.onerror =
                function() {
                    requestRunning = false;
                    button.disabled = false;

                    progressText.textContent =
                        'Network error';

                    showMessage(
                        'Network error during upload',
                        true
                    );
                };

            request.onabort =
                function() {
                    requestRunning = false;
                    button.disabled = false;

                    progressText.textContent =
                        'Upload cancelled';

                    showMessage(
                        'Upload was cancelled',
                        true
                    );
                };

            request.ontimeout =
                function() {
                    requestRunning = false;
                    button.disabled = false;

                    progressText.textContent =
                        'Upload timed out';

                    showMessage(
                        'Upload timed out',
                        true
                    );
                };

            request.open(
                'POST',
                '/api/upload'
            );

            request.timeout = 0;

            request.send(
                formData
            );
        }

        element('cameraImage').onload =
            function() {
                element(
                    'cameraImage'
                ).style.display =
                    'block';

                element(
                    'cameraMessage'
                ).style.display =
                    'none';
            };

        element('cameraImage').onerror =
            function() {
                element(
                    'cameraImage'
                ).style.display =
                    'none';

                element(
                    'cameraMessage'
                ).style.display =
                    'block';

                text(
                    'cameraMessage',
                    'Camera image unavailable'
                );
            };

        loadFiles();
        updateStatus();

        window.setInterval(
            updateStatus,
            1000
        );
    </script>
</body>
</html>
)HTML";