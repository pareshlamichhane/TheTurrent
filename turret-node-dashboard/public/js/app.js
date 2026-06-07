const socket = io();

const radarCanvas = document.getElementById("radarCanvas");
const ctx = radarCanvas.getContext("2d");

const connectionStatus = document.getElementById("connectionStatus");
const systemStatus = document.getElementById("systemStatus");

const alertStrip = document.getElementById("alertStrip");
const alertTitle = document.getElementById("alertTitle");
const alertMessage = document.getElementById("alertMessage");
const alertState = document.getElementById("alertState");

const angleText = document.getElementById("angleText");
const distanceText = document.getElementById("distanceText");
const servoText = document.getElementById("servoText");
const modeText = document.getElementById("modeText");
const autoStateFooter = document.getElementById("autoStateFooter");

const autoStateValue = document.getElementById("autoStateValue");
const emergencyStopValue = document.getElementById("emergencyStopValue");
const wifiValue = document.getElementById("wifiValue");
const setupApValue = document.getElementById("setupApValue");
const otaReadyValue = document.getElementById("otaReadyValue");

const fireAttemptsValue = document.getElementById("fireAttemptsValue");
const soilAttemptsValue = document.getElementById("soilAttemptsValue");
const soilWaterAngleValue = document.getElementById("soilWaterAngleValue");
const bestFlameAngleValue = document.getElementById("bestFlameAngleValue");
const bestFlameScoreValue = document.getElementById("bestFlameScoreValue");

const tempValue = document.getElementById("tempValue");
const humidityValue = document.getElementById("humidityValue");
const smokeValue = document.getElementById("smokeValue");
const soilValue = document.getElementById("soilValue");
const flameValue = document.getElementById("flameValue");
const flameAnalogValue = document.getElementById("flameAnalogValue");
const flameScoreValue = document.getElementById("flameScoreValue");
const nearWarningValue = document.getElementById("nearWarningValue");
const nearDangerValue = document.getElementById("nearDangerValue");
const nearPumpValue = document.getElementById("nearPumpValue");

const pumpValue = document.getElementById("pumpValue");
const buzzerValue = document.getElementById("buzzerValue");
const greenLedValue = document.getElementById("greenLedValue");
const yellowLedValue = document.getElementById("yellowLedValue");
const redLedValue = document.getElementById("redLedValue");
const packetValue = document.getElementById("packetValue");

const eventLog = document.getElementById("eventLog");
const esp32IpInput = document.getElementById("esp32IpInput");

let currentAngle = 90;
let currentDistance = 250;
let currentStatus = "SAFE";
let currentAutoState = "AUTO_SCAN";
let buzzerState = false;

let radarPoints = [];
let lastFlashKey = "";

function sendCommand(command) {
  socket.emit("command", command);
}

function setManualMode() {
  socket.emit("manualMode");
}

function setAutoMode() {
  socket.emit("autoMode");
}

function servoLeft() {
  currentAngle = Math.max(0, currentAngle - 10);

  sendCommand({
    type: "servo",
    angle: currentAngle
  });
}

function servoRight() {
  currentAngle = Math.min(180, currentAngle + 10);

  sendCommand({
    type: "servo",
    angle: currentAngle
  });
}

function pumpBurst() {
  sendCommand({
    type: "pump",
    value: true,
    ttl: 2000
  });
}

function pumpOff() {
  sendCommand({
    type: "pump",
    value: false
  });
}

function toggleBuzzer() {
  buzzerState = !buzzerState;

  sendCommand({
    type: "buzzer",
    value: buzzerState
  });
}

function setLed(green, yellow, red) {
  sendCommand({
    type: "led",
    green,
    yellow,
    red
  });
}

function emergencyStop() {
  socket.emit("emergencyStop");
}

function clearEmergency() {
  socket.emit("clearEmergency");
}

function updateStatusBadge(status) {
  systemStatus.textContent = status;

  systemStatus.classList.remove("safe", "warning", "danger");

  if (status === "DANGER") {
    systemStatus.classList.add("danger");
  } else if (status === "WARNING") {
    systemStatus.classList.add("warning");
  } else {
    systemStatus.classList.add("safe");
  }
}

function setAlertClass(className) {
  alertStrip.classList.remove(
    "safeFlash",
    "warningFlash",
    "dangerFlash",
    "fireFlash",
    "soilFlash",
    "nearFlash"
  );

  alertStrip.classList.add(className);

  alertStrip.style.animation = "none";
  alertStrip.offsetHeight;
  alertStrip.style.animation = "";
}

function updateAlert(data) {
  let className = "safeFlash";
  let title = "SYSTEM NORMAL";
  let message = "Radar scanning and environmental monitoring are active.";

  if (data.emergency_stop) {
    className = "dangerFlash";
    title = "EMERGENCY STOP ACTIVE";
    message = "Pump and buzzer are disabled. Clear emergency stop to resume operation.";
  } else if (!data.auto) {
    className = "warningFlash";
    title = "MANUAL CONTROL ACTIVE";
    message = "Operator override is controlling the turret.";
  } else if (data.near_warning) {
    className = "nearFlash";
    title = data.near_danger ? "NEAR OBJECT DANGER" : "NEAR OBJECT WARNING";
    message = `Object detected at ${data.distance_cm} cm. Warning threshold: ${data.near_warning_distance_cm} cm.`;
  }

  if (!data.emergency_stop && data.auto_state === "NEAR_DETERRENT") {
    className = "nearFlash";
    title = "NEAR DETERRENT ACTIVE";
    message = `Turret paused radar sweep and activated deterrent response at ${data.distance_cm} cm.`;
  }

  if (!data.emergency_stop && data.auto_state === "SOIL_WATER_MOVE") {
    className = "soilFlash";
    title = "SOIL WATERING POSITIONING";
    message = `Moving nozzle to soil watering angle ${data.soil_water_angle}°.`;
  }

  if (!data.emergency_stop && data.auto_state === "SOIL_WATERING") {
    className = "soilFlash";
    title = "SOIL WATERING ACTIVE";
    message = `Soil value ${data.soil}. Attempt ${data.soil_attempts}.`;
  }

  if (!data.emergency_stop && data.auto_state === "FIRE_SEARCH") {
    className = "fireFlash";
    title = "FIRE SOURCE SEARCH";
    message = `Scanning for strongest flame signal. Current flame score: ${data.flame_score}.`;
  }

  if (!data.emergency_stop && data.auto_state === "FIRE_EXTINGUISH") {
    className = "fireFlash";
    title = "FIRE EXTINGUISHING ACTIVE";
    message = `Locked angle ${data.best_flame_angle}°. Fire attempt ${data.fire_attempts}.`;
  }

  if (!data.emergency_stop && data.auto_state === "FIRE_RECHECK") {
    className = "fireFlash";
    title = "FIRE RECHECK ACTIVE";
    message = `Rechecking around flame angle ${data.best_flame_angle}°.`;
  }

  if (!data.emergency_stop && data.status === "DANGER" && className !== "fireFlash") {
    className = "dangerFlash";
    title = "DANGER CONDITION";
    message = `Danger detected. Smoke: ${data.smoke}, flame score: ${data.flame_score}.`;
  }

  if (!data.emergency_stop && data.status === "WARNING" && className === "safeFlash") {
    className = "warningFlash";
    title = "WARNING CONDITION";
    message = data.event || "Warning condition detected by sensors.";
  }

  if (!data.wifi_connected && data.setup_ap) {
    className = "warningFlash";
    title = "SETUP MODE ACTIVE";
    message = "ESP32 setup access point is active. Configure Wi-Fi from the ESP32 portal.";
  }

  const flashKey = [
    className,
    title,
    data.auto_state,
    data.status,
    data.event,
    data.near_warning,
    data.near_danger,
    data.emergency_stop
  ].join("|");

  alertTitle.textContent = title;
  alertMessage.textContent = message;
  alertState.textContent = data.auto_state || "UNKNOWN";

  if (flashKey !== lastFlashKey) {
    lastFlashKey = flashKey;
    setAlertClass(className);
  } else {
    alertStrip.classList.remove(
      "safeFlash",
      "warningFlash",
      "dangerFlash",
      "fireFlash",
      "soilFlash",
      "nearFlash"
    );

    alertStrip.classList.add(className);
  }
}

function drawRadar() {
  const w = radarCanvas.width;
  const h = radarCanvas.height;

  ctx.clearRect(0, 0, w, h);

  const cx = w / 2;
  const cy = h - 38;
  const maxRadius = Math.min(w / 2 - 48, h - 84);

  ctx.save();

  const background = ctx.createRadialGradient(cx, cy, 20, cx, cy, maxRadius);
  background.addColorStop(0, "rgba(0, 255, 150, 0.09)");
  background.addColorStop(1, "rgba(0, 255, 150, 0)");

  ctx.fillStyle = background;
  ctx.beginPath();
  ctx.arc(cx, cy, maxRadius, Math.PI, Math.PI * 2);
  ctx.fill();

  ctx.strokeStyle = "rgba(0, 255, 150, 0.35)";
  ctx.lineWidth = 1;

  for (let r = maxRadius / 5; r <= maxRadius; r += maxRadius / 5) {
    ctx.beginPath();
    ctx.arc(cx, cy, r, Math.PI, Math.PI * 2);
    ctx.stroke();

    ctx.fillStyle = "rgba(190, 255, 225, 0.72)";
    ctx.font = "12px Arial";
    ctx.fillText(`${Math.round((r / maxRadius) * 250)}cm`, cx + 10, cy - r + 14);
  }

  for (let a = 0; a <= 180; a += 15) {
    const rad = Math.PI - (a * Math.PI / 180);
    const x = cx + Math.cos(rad) * maxRadius;
    const y = cy - Math.sin(rad) * maxRadius;

    ctx.strokeStyle = a % 30 === 0
      ? "rgba(0, 255, 150, 0.35)"
      : "rgba(0, 255, 150, 0.13)";

    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(x, y);
    ctx.stroke();

    if (a % 30 === 0) {
      ctx.fillStyle = "rgba(220, 255, 240, 0.78)";
      ctx.font = "12px Arial";
      ctx.fillText(`${a}°`, x - 13, y - 6);
    }
  }

  radarPoints = radarPoints.filter((point) => Date.now() - point.time < 2800);

  for (const point of radarPoints) {
    const age = Date.now() - point.time;
    const alpha = Math.max(0, 1 - age / 2800);

    const rad = Math.PI - (point.angle * Math.PI / 180);
    const radius = (Math.min(point.distance, 250) / 250) * maxRadius;

    const px = cx + Math.cos(rad) * radius;
    const py = cy - Math.sin(rad) * radius;

    let color = `rgba(0,255,150,${alpha})`;

    if (point.status === "WARNING") {
      color = `rgba(255,209,102,${alpha})`;
    }

    if (point.status === "DANGER") {
      color = `rgba(255,77,77,${alpha})`;
    }

    if (
      point.autoState === "FIRE_SEARCH" ||
      point.autoState === "FIRE_EXTINGUISH" ||
      point.autoState === "FIRE_RECHECK"
    ) {
      color = `rgba(255,122,61,${alpha})`;
    }

    if (point.autoState === "SOIL_WATER_MOVE" || point.autoState === "SOIL_WATERING") {
      color = `rgba(183,121,63,${alpha})`;
    }

    if (point.nearDanger || point.autoState === "NEAR_DETERRENT") {
      color = `rgba(251,191,36,${alpha})`;
    }

    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.arc(px, py, point.nearDanger ? 9 : 5, 0, Math.PI * 2);
    ctx.fill();
  }

  const sweepRad = Math.PI - (currentAngle * Math.PI / 180);
  const sx = cx + Math.cos(sweepRad) * maxRadius;
  const sy = cy - Math.sin(sweepRad) * maxRadius;

  let sweepColor = "rgba(0,255,150,0.95)";

  if (currentStatus === "WARNING") {
    sweepColor = "rgba(255,209,102,0.95)";
  }

  if (currentStatus === "DANGER") {
    sweepColor = "rgba(255,77,77,0.95)";
  }

  if (
    currentAutoState === "FIRE_SEARCH" ||
    currentAutoState === "FIRE_EXTINGUISH" ||
    currentAutoState === "FIRE_RECHECK"
  ) {
    sweepColor = "rgba(255,122,61,0.98)";
  }

  if (currentAutoState === "SOIL_WATER_MOVE" || currentAutoState === "SOIL_WATERING") {
    sweepColor = "rgba(183,121,63,0.98)";
  }

  if (currentAutoState === "NEAR_DETERRENT") {
    sweepColor = "rgba(251,191,36,0.98)";
  }

  ctx.strokeStyle = sweepColor;
  ctx.lineWidth = 4;
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(sx, sy);
  ctx.stroke();

  const pointRadius = (Math.min(currentDistance, 250) / 250) * maxRadius;
  const px = cx + Math.cos(sweepRad) * pointRadius;
  const py = cy - Math.sin(sweepRad) * pointRadius;

  ctx.fillStyle = sweepColor;
  ctx.beginPath();
  ctx.arc(px, py, 10, 0, Math.PI * 2);
  ctx.fill();

  ctx.fillStyle = "#dfffee";
  ctx.beginPath();
  ctx.arc(cx, cy, 7, 0, Math.PI * 2);
  ctx.fill();

  ctx.restore();

  requestAnimationFrame(drawRadar);
}

function applyTelemetry(data) {
  currentAngle = Number(data.angle || 0);
  currentDistance = Number(data.distance_cm || 250);
  currentStatus = data.status || "SAFE";
  currentAutoState = data.auto_state || "UNKNOWN";

  radarPoints.push({
    angle: currentAngle,
    distance: currentDistance,
    status: currentStatus,
    autoState: currentAutoState,
    nearDanger: Boolean(data.near_danger),
    time: Date.now()
  });

  angleText.textContent = `${currentAngle}°`;
  distanceText.textContent = `${currentDistance} cm`;
  servoText.textContent = `${currentAngle}°`;
  modeText.textContent = data.auto ? "AUTO" : "MANUAL";
  autoStateFooter.textContent = currentAutoState;

  autoStateValue.textContent = currentAutoState;
  emergencyStopValue.textContent = data.emergency_stop ? "ACTIVE" : "CLEAR";
  wifiValue.textContent = data.wifi_connected ? "CONNECTED" : "OFFLINE";
  setupApValue.textContent = data.setup_ap ? "ACTIVE" : "OFF";
  otaReadyValue.textContent = data.ota_ready ? "YES" : "NO";

  fireAttemptsValue.textContent = data.fire_attempts ?? 0;
  soilAttemptsValue.textContent = data.soil_attempts ?? 0;
  soilWaterAngleValue.textContent = `${data.soil_water_angle ?? 90}°`;
  bestFlameAngleValue.textContent = `${data.best_flame_angle ?? 90}°`;
  bestFlameScoreValue.textContent = data.best_flame_score ?? 0;

  tempValue.textContent = `${data.temperature ?? 0} °C`;
  humidityValue.textContent = `${data.humidity ?? 0} %`;
  smokeValue.textContent = data.smoke ?? 0;
  soilValue.textContent = data.soil ?? 0;

  flameValue.textContent = data.flame ? "YES" : "NO";
  flameAnalogValue.textContent = data.flame_analog ?? 4095;
  flameScoreValue.textContent = data.flame_score ?? 0;

  nearWarningValue.textContent = data.near_warning ? "YES" : "NO";
  nearDangerValue.textContent = data.near_danger ? "YES" : "NO";
  nearPumpValue.textContent = data.near_pump_deterrent_enabled ? "ENABLED" : "DISABLED";

  pumpValue.textContent = data.pump ? "ON" : "OFF";
  buzzerValue.textContent = data.buzzer ? "ON" : "OFF";
  greenLedValue.textContent = data.green ? "ON" : "OFF";
  yellowLedValue.textContent = data.yellow ? "ON" : "OFF";
  redLedValue.textContent = data.red ? "ON" : "OFF";
  packetValue.textContent = data.packetCount ?? 0;

  buzzerState = Boolean(data.buzzer);

  updateStatusBadge(currentStatus);
  updateAlert(data);
}

function renderEvents(events) {
  eventLog.innerHTML = "";

  if (!events || events.length === 0) {
    const item = document.createElement("div");
    item.className = "eventItem";
    item.textContent = "No events yet.";
    eventLog.appendChild(item);
    return;
  }

  for (const event of events) {
    const category = event.category || "system";

    const item = document.createElement("div");
    item.className = "eventItem";

    item.innerHTML = `
      <div class="eventTop">
        <span class="eventTime">${event.time}</span>
        <span class="eventCategory category-${category}">${category}</span>
      </div>
      <div>${event.text}</div>
    `;

    eventLog.appendChild(item);
  }
}

socket.on("init", (data) => {
  if (data.esp32Ip) {
    esp32IpInput.value = data.esp32Ip;
  }

  renderEvents(data.eventHistory || []);

  if (data.latestTelemetry) {
    applyTelemetry(data.latestTelemetry);
  }
});

socket.on("connectionStatus", (data) => {
  if (data.connected) {
    connectionStatus.textContent = "ONLINE";
    connectionStatus.classList.remove("disconnected");
    connectionStatus.classList.add("connected");
  } else {
    connectionStatus.textContent = "OFFLINE";
    connectionStatus.classList.remove("connected");
    connectionStatus.classList.add("disconnected");

    alertTitle.textContent = "TELEMETRY OFFLINE";
    alertMessage.textContent = "No recent UDP packets from the turret controller.";
    alertState.textContent = "DISCONNECTED";
    setAlertClass("dangerFlash");
  }

  if (data.esp32Ip) {
    esp32IpInput.value = data.esp32Ip;
  }

  packetValue.textContent = data.packetCount ?? 0;
});

socket.on("telemetry", (data) => {
  applyTelemetry(data);
});

socket.on("eventHistory", (events) => {
  renderEvents(events);
});

document.getElementById("btnManual").addEventListener("click", setManualMode);
document.getElementById("btnAuto").addEventListener("click", setAutoMode);
document.getElementById("btnEmergencyStop").addEventListener("click", emergencyStop);
document.getElementById("btnClearEmergency").addEventListener("click", clearEmergency);
document.getElementById("btnLeft").addEventListener("click", servoLeft);
document.getElementById("btnRight").addEventListener("click", servoRight);
document.getElementById("btnPumpOn").addEventListener("click", pumpBurst);
document.getElementById("btnPumpOff").addEventListener("click", pumpOff);
document.getElementById("btnBuzzer").addEventListener("click", toggleBuzzer);

document.getElementById("btnLedGreen").addEventListener("click", () => {
  setLed(true, false, false);
});

document.getElementById("btnLedYellow").addEventListener("click", () => {
  setLed(false, true, false);
});

document.getElementById("btnLedRed").addEventListener("click", () => {
  setLed(false, false, true);
});

document.getElementById("btnSetIp").addEventListener("click", () => {
  const ip = esp32IpInput.value.trim();

  if (!ip) {
    return;
  }

  socket.emit("setEsp32Ip", ip);
});

document.addEventListener("keydown", (event) => {
  const key = event.key.toLowerCase();

  if (event.repeat) return;

  if (key === "m") setManualMode();
  if (key === "x") setAutoMode();
  if (key === "a") servoLeft();
  if (key === "d") servoRight();
  if (key === "w") pumpBurst();
  if (key === "s") pumpOff();
  if (key === "b") toggleBuzzer();

  if (key === "1") {
    setLed(true, false, false);
  }

  if (key === "2") {
    setLed(false, true, false);
  }

  if (key === "3") {
    setLed(false, false, true);
  }
});

drawRadar();