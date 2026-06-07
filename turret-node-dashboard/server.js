const express = require("express");
const http = require("http");
const socketIo = require("socket.io");
const dgram = require("dgram");
const os = require("os");

const app = express();
const server = http.createServer(app);
const io = socketIo(server);

const WEB_PORT = 3000;

// ESP32 -> Node.js telemetry
const TELEMETRY_PORT = 4210;

// Node.js -> ESP32 commands
const ESP32_COMMAND_PORT = 4212;

// Updated automatically when ESP32 telemetry arrives.
let esp32Ip = "192.168.1.50";

let latestTelemetry = null;
let eventHistory = [];
let previousEvent = "";
let lastPacketAt = 0;
let packetCount = 0;

const telemetryServer = dgram.createSocket("udp4");
const commandClient = dgram.createSocket("udp4");

app.use(express.static("public"));

function getLocalIpAddresses() {
  const interfaces = os.networkInterfaces();
  const results = [];

  for (const name of Object.keys(interfaces)) {
    for (const net of interfaces[name]) {
      if (net.family === "IPv4" && !net.internal) {
        results.push(net.address);
      }
    }
  }

  return results;
}

function addEvent(text, category = "system") {
  if (!text) return;

  const event = {
    id: Date.now() + Math.random(),
    time: new Date().toLocaleTimeString(),
    category,
    text
  };

  eventHistory.unshift(event);

  if (eventHistory.length > 120) {
    eventHistory.pop();
  }

  io.emit("eventHistory", eventHistory);
}

function sendCommand(command) {
  const message = Buffer.from(JSON.stringify(command));

  commandClient.send(message, ESP32_COMMAND_PORT, esp32Ip, (error) => {
    if (error) {
      console.error("UDP command send error:", error.message);
      addEvent(`Command send error: ${error.message}`, "error");
    }
  });
}

function normalizeTelemetry(data, remoteAddress) {
  return {
    type: data.type || "telemetry",
    device: data.device || "environmental_turret",

    millis: Number(data.millis ?? 0),
    receivedAt: Date.now(),

    esp32Ip: remoteAddress,
    ip: data.ip || remoteAddress,

    wifi_connected: Boolean(data.wifi_connected),
    setup_ap: Boolean(data.setup_ap),
    emergency_stop: Boolean(data.emergency_stop),
    ota_ready: Boolean(data.ota_ready),

    auto: data.auto !== false,
    auto_state: data.auto_state || "UNKNOWN",
    status: data.status || "SAFE",

    angle: Number(data.angle ?? 0),
    distance_cm: Number(data.distance_cm ?? 250),

    near_warning: Boolean(data.near_warning),
    near_danger: Boolean(data.near_danger),
    near_warning_enabled: Boolean(data.near_warning_enabled),
    near_pump_deterrent_enabled: Boolean(data.near_pump_deterrent_enabled),
    near_warning_distance_cm: Number(data.near_warning_distance_cm ?? 40),
    near_danger_distance_cm: Number(data.near_danger_distance_cm ?? 20),

    temperature: Number(data.temperature ?? 0),
    humidity: Number(data.humidity ?? 0),
    smoke: Number(data.smoke ?? 0),
    soil: Number(data.soil ?? 0),

    flame: Boolean(data.flame),
    flame_analog: Number(data.flame_analog ?? 4095),
    flame_score: Number(data.flame_score ?? 0),

    best_flame_angle: Number(data.best_flame_angle ?? 90),
    best_flame_score: Number(data.best_flame_score ?? 0),

    pump: Boolean(data.pump),
    buzzer: Boolean(data.buzzer),

    green: Boolean(data.green),
    yellow: Boolean(data.yellow),
    red: Boolean(data.red),

    soil_water_angle: Number(data.soil_water_angle ?? 90),
    fire_attempts: Number(data.fire_attempts ?? 0),
    soil_attempts: Number(data.soil_attempts ?? 0),

    event: data.event || "",
    last_command: data.last_command || "none",

    packetCount
  };
}

function getEventCategory(telemetry) {
  if (telemetry.emergency_stop) {
    return "danger";
  }

  if (
    telemetry.auto_state === "FIRE_SEARCH" ||
    telemetry.auto_state === "FIRE_EXTINGUISH" ||
    telemetry.auto_state === "FIRE_RECHECK" ||
    telemetry.flame ||
    telemetry.status === "DANGER"
  ) {
    return "danger";
  }

  if (
    telemetry.auto_state === "SOIL_WATER_MOVE" ||
    telemetry.auto_state === "SOIL_WATERING"
  ) {
    return "soil";
  }

  if (
    telemetry.auto_state === "NEAR_DETERRENT" ||
    telemetry.near_warning ||
    telemetry.near_danger
  ) {
    return "near";
  }

  if (telemetry.status === "WARNING") {
    return "warning";
  }

  return "system";
}

telemetryServer.on("message", (message, remote) => {
  try {
    const raw = JSON.parse(message.toString());

    packetCount++;
    lastPacketAt = Date.now();

    // Auto-detect ESP32 IP from UDP source.
    esp32Ip = remote.address;

    const telemetry = normalizeTelemetry(raw, remote.address);
    latestTelemetry = telemetry;

    if (telemetry.event && telemetry.event !== previousEvent) {
      previousEvent = telemetry.event;
      addEvent(telemetry.event, getEventCategory(telemetry));
    }

    io.emit("telemetry", telemetry);
  } catch (error) {
    console.log("Invalid UDP packet:", message.toString());
  }
});

telemetryServer.on("listening", () => {
  const address = telemetryServer.address();

  console.log("--------------------------------------");
  console.log("Autonomous Environmental Safety Turret");
  console.log("--------------------------------------");
  console.log(`Dashboard: http://localhost:${WEB_PORT}`);
  console.log(`Telemetry UDP: ${address.address}:${address.port}`);
  console.log(`Command UDP target port: ${ESP32_COMMAND_PORT}`);
  console.log("--------------------------------------");
  console.log("Laptop IP addresses for ESP32 config:");

  for (const ip of getLocalIpAddresses()) {
    console.log(`- ${ip}`);
  }

  console.log("--------------------------------------");
});

telemetryServer.bind(TELEMETRY_PORT);

io.on("connection", (socket) => {
  console.log("Dashboard client connected");

  socket.emit("init", {
    esp32Ip,
    telemetryPort: TELEMETRY_PORT,
    commandPort: ESP32_COMMAND_PORT,
    localIps: getLocalIpAddresses(),
    latestTelemetry,
    eventHistory
  });

  socket.on("setEsp32Ip", (ip) => {
    if (!ip || typeof ip !== "string") return;

    esp32Ip = ip.trim();
    addEvent(`Controller target updated to ${esp32Ip}`, "system");

    io.emit("esp32Ip", esp32Ip);
  });

  socket.on("command", (command) => {
    if (!command || typeof command !== "object") return;

    sendCommand(command);
    io.emit("lastCommand", command);
  });

  socket.on("manualMode", () => {
    sendCommand({
      type: "mode",
      auto: false
    });

    addEvent("Manual control mode requested", "control");
  });

  socket.on("autoMode", () => {
    sendCommand({
      type: "mode",
      auto: true
    });

    addEvent("Autonomous mode requested", "control");
  });

  socket.on("emergencyStop", () => {
    sendCommand({
      type: "emergency_stop"
    });

    addEvent("Emergency stop command sent", "danger");
  });

  socket.on("clearEmergency", () => {
    sendCommand({
      type: "clear_emergency"
    });

    addEvent("Emergency stop clear command sent", "control");
  });

  socket.on("servoAngle", (angle) => {
    const safeAngle = Math.max(0, Math.min(180, Number(angle)));

    sendCommand({
      type: "servo",
      angle: safeAngle
    });
  });

  socket.on("pumpBurst", (ttl = 2000) => {
    const safeTtl = Math.max(300, Math.min(10000, Number(ttl)));

    sendCommand({
      type: "pump",
      value: true,
      ttl: safeTtl
    });
  });

  socket.on("pumpOff", () => {
    sendCommand({
      type: "pump",
      value: false
    });
  });
});

setInterval(() => {
  const connected = Date.now() - lastPacketAt < 2000;

  io.emit("connectionStatus", {
    connected,
    lastPacketAt,
    esp32Ip,
    packetCount
  });
}, 500);

server.listen(WEB_PORT, () => {
  console.log(`Web server running on http://localhost:${WEB_PORT}`);
});