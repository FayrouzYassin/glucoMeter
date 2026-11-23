
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include "MAX30105.h"

// ==================================================
// WIFI SETTINGS
// ==================================================

const char* ssid = "PPG_Recorder";

IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

const byte DNS_PORT = 53;

DNSServer dnsServer;
WebServer server(80);

// ==================================================
// SENSOR SETTINGS
// ==================================================

MAX30105 particleSensor;

const int SDA_PIN = 21;
const int SCL_PIN = 22;

const uint32_t SAMPLE_RATE = 400;

// ==================================================
// SAMPLE BUFFER
// ==================================================

// 16384 samples = approximately 40.96 seconds at 400 Hz

const uint32_t BUFFER_SIZE = 2048;

struct Sample {
  uint32_t index;
  uint32_t red;
  uint32_t ir;
};

Sample sampleBuffer[BUFFER_SIZE];

uint32_t writePosition = 0;
uint32_t storedSamples = 0;
uint32_t totalSamples = 0;

// ==================================================
// WEBSITE
// ==================================================

const char webpage[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html>

<head>

<meta name="viewport"
content="width=device-width, initial-scale=1">

<title>PPG Recorder</title>

<style>

body {
  font-family: Arial, sans-serif;
  background: #f2f4f7;
  margin: 0;
  padding: 12px;
  text-align: center;
}

.container {
  max-width: 850px;
  margin: auto;
}

h2 {
  color: #222;
}

.panel {
  background: white;
  padding: 15px;
  margin: 12px 0;
  border-radius: 12px;
  box-shadow: 0 2px 7px #0001;
}

canvas {
  width: 100%;
  height: 180px;
  border: 1px solid #ddd;
  border-radius: 6px;
}

input {
  width: 95%;
  max-width: 400px;
  padding: 12px;
  font-size: 16px;
  margin: 8px;
  box-sizing: border-box;
}

button {
  padding: 12px 18px;
  margin: 5px;
  border: none;
  border-radius: 7px;
  color: white;
  font-size: 15px;
  cursor: pointer;
}

.start {
  background: #198754;
}

.stop {
  background: #dc3545;
}

.discard {
  background: #6c757d;
}

.download {
  background: #0d6efd;
}

#timer {
  font-size: 42px;
  font-weight: bold;
  color: #198754;
  margin: 12px;
  font-variant-numeric: tabular-nums;
}

#status {
  font-weight: bold;
}

#warning {
  color: #b45309;
  font-size: 14px;
}

.small {
  color: #666;
  font-size: 13px;
}

.progress {
  background: #e5e7eb;
  height: 10px;
  border-radius: 10px;
  overflow: hidden;
  margin: 12px 5px;
}

.progressFill {
  background: #198754;
  height: 100%;
  width: 0%;
  transition: width 0.1s linear;
}

</style>

</head>

<body>

<div class="container">

<h2>MAX30102 PPG Recorder</h2>

<div class="panel">

<p id="status">
Connecting to ESP32...
</p>

<p id="warning"></p>

<p>
Samples received:
<strong id="sampleCount">0</strong>
</p>

</div>

<div class="panel">

<h3>Recording Timer</h3>

<div id="timer">00:30</div>

<div class="progress">
  <div class="progressFill" id="progressFill"></div>
</div>

<p id="recordStatus">
Not recording
</p>

<p>
Recording duration: 30 seconds
</p>

</div>

<div class="panel">

<h3>Red Signal</h3>

<canvas id="redCanvas"></canvas>

</div>

<div class="panel">

<h3>Infrared (IR) Signal</h3>

<canvas id="irCanvas"></canvas>

</div>

<div class="panel">

<h3>Recording Controls</h3>

<input
  id="glucose"
  type="text"
  placeholder="Measured glucose level (e.g. 105 mg/dL)"
>

<br>

<button class="start" onclick="startRecording()">
Start 30-Second Recording
</button>

<button class="stop" onclick="stopRecording()">
Stop
</button>

<button class="discard" onclick="discardRecording()">
Discard
</button>

<button class="download" onclick="downloadCSV()">
Download CSV
</button>

<p>
Recorded samples:
<strong id="recordCount">0</strong>
</p>

</div>

<p class="small">
Wi-Fi: PPG_Recorder
<br>
Website: http://192.168.4.1
</p>

</div>

<script>

// ==================================================
// RECORDING SETTINGS
// ==================================================

const SAMPLE_RATE = 400;

const RECORDING_SECONDS = 30;

const REQUIRED_SAMPLES =
  SAMPLE_RATE * RECORDING_SECONDS;

// ==================================================
// VARIABLES
// ==================================================

let lastIndex = -1;

let redValues = [];
let irValues = [];

let recording = false;
let captureActive = false;

let recordedRows = [];

let recordingStartIndex = 0;
let recordingEndIndex = 0;

let recordingStartTime = 0;
let glucoseLabel = "";

let timerInterval = null;

const MAX_GRAPH_POINTS = 500;

// ==================================================
// GRAPH DRAWING
// ==================================================

function drawGraph(canvasId, values) {

  const canvas =
    document.getElementById(canvasId);

  const ctx = canvas.getContext("2d");

  const width = canvas.clientWidth;
  const height = canvas.clientHeight;

  const dpr = window.devicePixelRatio || 1;

  if (
    canvas.width !== width * dpr ||
    canvas.height !== height * dpr
  ) {

    canvas.width = width * dpr;
    canvas.height = height * dpr;

  }

  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

  ctx.clearRect(0, 0, width, height);

  if (values.length < 2) return;

  let min = Math.min(...values);
  let max = Math.max(...values);

  if (min === max) {
    min -= 1;
    max += 1;
  }

  const padding = 10;

  ctx.beginPath();

  ctx.strokeStyle = "#dddddd";
  ctx.lineWidth = 1;

  for (let i = 1; i < 4; i++) {

    const y = height * i / 4;

    ctx.moveTo(0, y);
    ctx.lineTo(width, y);

  }

  ctx.stroke();

  ctx.beginPath();

  ctx.strokeStyle =
    canvasId === "redCanvas"
      ? "#dc3545"
      : "#0d6efd";

  ctx.lineWidth = 1.5;

  values.forEach((value, i) => {

    const x =
      padding +
      i * (width - 2 * padding) /
      (values.length - 1);

    const y =
      height - padding -
      ((value - min) / (max - min)) *
      (height - 2 * padding);

    if (i === 0) {
      ctx.moveTo(x, y);
    } else {
      ctx.lineTo(x, y);
    }

  });

  ctx.stroke();

}

// ==================================================
// TIMER
// ==================================================

function formatTime(seconds) {

  const mins = Math.floor(seconds / 60);

  const secs = Math.floor(seconds % 60);

  return (
    String(mins).padStart(2, "0") +
    ":" +
    String(secs).padStart(2, "0")
  );

}

function updateTimer() {

  if (!recording) return;

  const elapsed =
    (Date.now() - recordingStartTime) / 1000;

  const remaining =
    Math.max(0, RECORDING_SECONDS - elapsed);

  document.getElementById("timer").textContent =
    formatTime(remaining);

  const progress =
    Math.min(100, elapsed / RECORDING_SECONDS * 100);

  document.getElementById("progressFill").style.width =
    progress + "%";

  if (elapsed >= RECORDING_SECONDS) {

    recording = false;

    clearInterval(timerInterval);

    document.getElementById("recordStatus").textContent =
      "30 seconds reached - finishing data transfer";

    document.getElementById("timer").textContent =
      "00:00";

    // Keep captureActive true until the final
    // target sample has been received.

    if (lastIndex >= recordingEndIndex) {
      finishRecording();
    }

  }

}

function finishRecording() {

  captureActive = false;
  recording = false;

  clearInterval(timerInterval);

  document.getElementById("recordStatus").textContent =
    "Recording complete";

  document.getElementById("timer").textContent =
    "00:00";

}

// ==================================================
// START RECORDING
// ==================================================

function startRecording() {

  if (recording || captureActive) {
    alert("A recording is already in progress.");
    return;
  }

  glucoseLabel =
    document.getElementById("glucose").value.trim();

  if (!glucoseLabel) {

    alert("Please enter the measured glucose level.");

    return;

  }

  recordedRows = [];

  recordingStartIndex = lastIndex + 1;

  recordingEndIndex =
    recordingStartIndex + REQUIRED_SAMPLES - 1;

  recordingStartTime = Date.now();

  recording = true;
  captureActive = true;

  document.getElementById("recordCount").textContent =
    "0";

  document.getElementById("warning").textContent =
    "";

  document.getElementById("recordStatus").textContent =
    "Recording...";

  document.getElementById("timer").textContent =
    "00:30";

  document.getElementById("progressFill").style.width =
    "0%";

  clearInterval(timerInterval);

  timerInterval = setInterval(updateTimer, 100);

}

// ==================================================
// MANUAL STOP
// ==================================================

function stopRecording() {

  if (!recording && !captureActive) return;

  recording = false;
  captureActive = false;

  clearInterval(timerInterval);

  document.getElementById("recordStatus").textContent =
    "Manually stopped";

  document.getElementById("timer").textContent =
    "Stopped";

}

// ==================================================
// DISCARD RECORDING
// ==================================================

function discardRecording() {

  recording = false;
  captureActive = false;

  clearInterval(timerInterval);

  recordedRows = [];

  document.getElementById("recordCount").textContent =
    "0";

  document.getElementById("recordStatus").textContent =
    "Recording discarded";

  document.getElementById("timer").textContent =
    "00:30";

  document.getElementById("progressFill").style.width =
    "0%";

  document.getElementById("warning").textContent =
    "";

}

// ==================================================
// RECEIVE SENSOR DATA
// ==================================================

async function getData() {

  try {

    const response = await fetch(
      "/data?after=" + lastIndex,
      { cache: "no-store" }
    );

    if (!response.ok) {
      throw new Error("HTTP " + response.status);
    }

    const data = await response.json();

    if (data.gap) {

      document.getElementById("warning").textContent =
        "WARNING: Samples were overwritten before transfer. " +
        "This recording may be incomplete.";

    }

    for (const sample of data.samples) {

      const index = sample[0];
      const red = sample[1];
      const ir = sample[2];

      lastIndex = index;

      redValues.push(red);
      irValues.push(ir);

      if (redValues.length > MAX_GRAPH_POINTS) {

        redValues.shift();
        irValues.shift();

      }

      // Save only samples belonging to this recording.
      // The automatic recording is defined by exactly
      // 12000 nominal sample indices.

      if (
        captureActive &&
        index >= recordingStartIndex &&
        index <= recordingEndIndex
      ) {

        const elapsed =
          (index - recordingStartIndex) / SAMPLE_RATE;

        const timestamp = new Date(
          recordingStartTime + elapsed * 1000
        ).toISOString();

        recordedRows.push({

          sample_index: index,

          timestamp: timestamp,

          elapsed_seconds: elapsed,

          red: red,

          ir: ir,

          glucose: glucoseLabel

        });

      }

    }

    document.getElementById("sampleCount").textContent =
      lastIndex + 1;

    document.getElementById("recordCount").textContent =
      recordedRows.length;

    drawGraph("redCanvas", redValues);

    drawGraph("irCanvas", irValues);

    document.getElementById("status").textContent =
      "ESP32 connected - receiving data";

    // Finish after the final requested sample arrives.
    if (
      captureActive &&
      lastIndex >= recordingEndIndex
    ) {

      finishRecording();

    }

  } catch (error) {

    document.getElementById("status").textContent =
      "Connection problem: " + error.message;

  }

}

// ==================================================
// CSV EXPORT
// ==================================================

function csvEscape(value) {

  return '"' +
    String(value).replace(/"/g, '""') +
    '"';

}

function downloadCSV() {

  if (recordedRows.length === 0) {

    alert("No recorded samples available.");

    return;

  }

  let csv =
    "sample_index,timestamp_iso,elapsed_seconds,red,ir,glucose_label\n";

  for (const row of recordedRows) {

    csv += [

      row.sample_index,

      csvEscape(row.timestamp),

      row.elapsed_seconds.toFixed(4),

      row.red,

      row.ir,

      csvEscape(row.glucose)

    ].join(",") + "\n";

  }

  const blob = new Blob(
    [csv],
    { type: "text/csv;charset=utf-8;" }
  );

  const url = URL.createObjectURL(blob);

  const link = document.createElement("a");

  const filename =
    "PPG_recording_" +
    new Date().toISOString().replace(/[:.]/g, "-") +
    ".csv";

  link.href = url;
  link.download = filename;

  document.body.appendChild(link);

  link.click();

  document.body.removeChild(link);

  URL.revokeObjectURL(url);

}

// ==================================================
// POLLING LOOP
// ==================================================

async function pollingLoop() {

  await getData();

  setTimeout(pollingLoop, 50);

}

pollingLoop();

</script>

</body>
</html>

)rawliteral";

// ==================================================
// STORE SENSOR SAMPLE
// ==================================================

void storeSample(uint32_t red, uint32_t ir) {

  sampleBuffer[writePosition].index = totalSamples;

  sampleBuffer[writePosition].red = red;

  sampleBuffer[writePosition].ir = ir;

  writePosition =
    (writePosition + 1) % BUFFER_SIZE;

  if (storedSamples < BUFFER_SIZE) {
    storedSamples++;
  }

  totalSamples++;

}

// ==================================================
// READ SENSOR
// ==================================================

void collectSensorData() {

  particleSensor.check();

  while (particleSensor.available()) {

    uint32_t red = particleSensor.getRed();

    uint32_t ir = particleSensor.getIR();

    storeSample(red, ir);

    particleSensor.nextSample();

  }

}

// ==================================================
// WEBSITE HANDLERS
// ==================================================

void handleRoot() {

  server.send_P(
    200,
    "text/html",
    webpage
  );

}

void handleData() {

  long after = -1;

  if (server.hasArg("after")) {

    after = server.arg("after").toInt();

  }

  uint32_t firstIndex =
    totalSamples - storedSamples;

  long lastIndex =
    totalSamples > 0
      ? (long)(totalSamples - 1)
      : -1;

  bool gap =
    after < (long)firstIndex - 1;

  uint32_t startIndex;

  if (after < (long)firstIndex) {

    startIndex = firstIndex;

  } else {

    startIndex = (uint32_t)(after + 1);

  }

  String json;

  json.reserve(16000);

  json = "{";

  json += "\"first\":";
  json += String(firstIndex);

  json += ",\"last\":";
  json += String(lastIndex);

  json += ",\"gap\":";
  json += gap ? "true" : "false";

  json += ",\"samples\":[";

  int sent = 0;

  const int MAX_BATCH = 250;

  for (
    uint32_t index = startIndex;
    index < totalSamples && sent < MAX_BATCH;
    index++
  ) {

    uint32_t position = index % BUFFER_SIZE;

    if (sent > 0) {
      json += ",";
    }

    json += "[";

    json += String(sampleBuffer[position].index);
    json += ",";
    json += String(sampleBuffer[position].red);
    json += ",";
    json += String(sampleBuffer[position].ir);

    json += "]";

    sent++;

  }

  json += "]}";

  server.send(
    200,
    "application/json",
    json
  );

}

// ==================================================
// CAPTIVE PORTAL HANDLERS
// ==================================================

void handleNotFound() {

  server.sendHeader(
    "Location",
    String("http://") + apIP.toString() + "/",
    true
  );

  server.send(
    302,
    "text/plain",
    ""
  );

}

// ==================================================
// SETUP
// ==================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();

  Serial.println("Starting PPG Recorder...");

  // ---------------- I2C ----------------

  Wire.begin(SDA_PIN, SCL_PIN);

  Wire.setClock(400000);

  // ---------------- MAX30102 ----------------

  Serial.println("Initializing MAX30102...");

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {

    Serial.println("ERROR: MAX30102 not found.");

    Serial.println("Check wiring and power.");

    while (1) {
      delay(1000);
    }

  }

  // LED power = 0x1F
  // Averaging = 1
  // LED mode = 2 (Red + IR)
  // Sample rate = 400 Hz
  // Pulse width = 411 us
  // ADC range = 4096

  particleSensor.setup(
    0x1F,
    1,
    2,
    400,
    411,
    4096
  );

  Serial.println("MAX30102 initialized.");

  // ---------------- Wi-Fi AP ----------------

  WiFi.mode(WIFI_AP);

  WiFi.softAPConfig(
    apIP,
    apIP,
    netMsk
  );

  // No password: open Wi-Fi network

  WiFi.softAP(ssid);

  Serial.println("Open Wi-Fi hotspot started.");

  Serial.print("Network: ");
  Serial.println(ssid);

  Serial.print("Website: http://");
  Serial.println(WiFi.softAPIP());

  // ---------------- DNS captive portal ----------------

  dnsServer.start(
    DNS_PORT,
    "*",
    apIP
  );

  Serial.println("Captive portal DNS started.");

  // ---------------- Web server ----------------

  server.on("/", handleRoot);

  server.on("/data", handleData);

  // Common phone/laptop captive portal checks

  server.on("/generate_204", handleRoot);

  server.on("/gen_204", handleRoot);

  server.on("/hotspot-detect.html", handleRoot);

  server.on("/connecttest.txt", handleRoot);

  server.on("/ncsi.txt", handleRoot);

  server.on("/canonical.html", handleRoot);

  server.on("/success.txt", handleRoot);

  server.on("/fwlink", handleRoot);

  server.onNotFound(handleNotFound);

  server.begin();

  Serial.println("Web server started.");

  Serial.println("PPG Recorder is ready.");

}

// ==================================================
// MAIN LOOP
// ==================================================

void loop() {

  dnsServer.processNextRequest();

  collectSensorData();

  server.handleClient();

}
