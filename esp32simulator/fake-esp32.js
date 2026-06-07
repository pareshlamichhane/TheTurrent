const dgram = require("dgram");

const telemetrySocket = dgram.createSocket("udp4");
const commandSocket = dgram.createSocket("udp4");

const DASHBOARD_IP = "127.0.0.1";

const DASHBOARD_TELEMETRY_PORT = 4210;
const FAKE_ESP32_COMMAND_PORT = 4212;

const config = {
  maxDistanceCm: 250,

  smokeWarningThreshold: 1200,
  smokeDangerThreshold: 2000,

  tempWarningThreshold: 40,
  tempDangerThreshold: 50,

  scanMinAngle: 0,
  scanMaxAngle: 180,
  scanStep: 3,
  scanIntervalMs: 60,

  fireSearchMinAngle: 0,
  fireSearchMaxAngle: 180,
  fireSearchStep: 5,

  flameStrongIsLower: true,
  flameAnalogThreshold: 1600,

  firePumpBurstMs: 2500,
  fireSettleMs: 1200,
  maxFireAttempts: 5,

  soilDryThreshold: 2500,
  soilWetTargetThreshold: 1800,
  soilWaterAngle: 90,

  soilPumpBurstMs: 2500,
  soilSettleMs: 2000,
  maxSoilAttempts: 3,

  nearWarningEnabled: true,
  nearPumpDeterrentEnabled: true,

  nearWarningDistanceCm: 40,
  nearDangerDistanceCm: 20,

  nearPumpBurstMs: 700,
  nearCooldownMs: 5000,

  maxManualPumpRunMs: 10000,
  manualControlTimeoutMs: 8000
};

const AutoState = {
  AUTO_SCAN: "AUTO_SCAN",
  FIRE_SEARCH: "FIRE_SEARCH",
  FIRE_EXTINGUISH: "FIRE_EXTINGUISH",
  SOIL_WATER_MOVE: "SOIL_WATER_MOVE",
  SOIL_WATERING: "SOIL_WATERING",
  NEAR_DETERRENT: "NEAR_DETERRENT"
};

let autoMode = true;
let autoState = AutoState.AUTO_SCAN;
let status = "SAFE";

let angle = 0;
let scanDirection = 1;

let distanceCm = 180;
let temperature = 28.5;
let humidity = 60;
let smoke = 500;
let soil = 1800;

let flame = false;
let flameAnalog = 4095;
let flameScore = 0;

let bestFlameAngle = 90;
let bestFlameScore = 0;
let fireSearchAngle = 0;
let fireAttempts = 0;

let soilAttempts = 0;

let pump = false;
let buzzer = false;

let green = true;
let yellow = false;
let red = false;

let latestEvent = "Fake ESP32 simulator started";
let lastCommand = "none";

let actionTimer = 0;
let firePumpRunning = false;
let soilPumpRunning = false;

let manualPumpStopAt = 0;
let manualLastSeen = 0;

let simulatedFireAngle = 75;
let simulatedFireActive = false;
let simulatedSoilDryMode = false;
let simulatedNearObjectActive = false;

let lastScanMoveAt = 0;

let lastNearDeterrentTime = 0;
let nearPumpRunning = false;

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function randomBetween(min, max) {
  return min + Math.random() * (max - min);
}

function randomInt(min, max) {
  return Math.floor(randomBetween(min, max + 1));
}

function setEvent(text) {
  latestEvent = text;
  console.log("[EVENT]", text);
}

function changeAutoState(newState, eventText) {
  autoState = newState;
  actionTimer = 0;
  firePumpRunning = false;
  soilPumpRunning = false;
  nearPumpRunning = false;

  setEvent(eventText);
}

function getFlameScore(value) {
  if (config.flameStrongIsLower) {
    return 4095 - value;
  }

  return value;
}

function isFlameStrongEnough() {
  if (config.flameStrongIsLower) {
    return flameAnalog <= config.flameAnalogThreshold;
  }

  return flameAnalog >= config.flameAnalogThreshold;
}

function isSmokeWarning() {
  return smoke >= config.smokeWarningThreshold;
}

function isSmokeDanger() {
  return smoke >= config.smokeDangerThreshold;
}

function isTempWarning() {
  return temperature >= config.tempWarningThreshold;
}

function isTempDanger() {
  return temperature >= config.tempDangerThreshold;
}

function isSoilDry() {
  return soil >= config.soilDryThreshold;
}

function isSoilWetEnough() {
  return soil <= config.soilWetTargetThreshold;
}

function isFireOrSmokeDetected() {
  return flame || isFlameStrongEnough() || isSmokeDanger();
}

function isNearWarning() {
  if (!config.nearWarningEnabled) {
    return false;
  }

  return distanceCm <= config.nearWarningDistanceCm;
}

function isNearDanger() {
  if (!config.nearWarningEnabled) {
    return false;
  }

  return distanceCm <= config.nearDangerDistanceCm;
}

function canRunNearDeterrent() {
  if (!config.nearPumpDeterrentEnabled) {
    return false;
  }

  return Date.now() - lastNearDeterrentTime >= config.nearCooldownMs;
}

function applyPump(value) {
  pump = Boolean(value);
}

function applyBuzzer(value) {
  buzzer = Boolean(value);
}

function applyLeds(g, y, r) {
  green = Boolean(g);
  yellow = Boolean(y);
  red = Boolean(r);
}

function updateStatus() {
  const danger = isFireOrSmokeDetected() || isTempDanger();
  const warning = isSmokeWarning() || isTempWarning() || isSoilDry() || isNearWarning();

  if (danger) {
    status = "DANGER";
  } else if (warning) {
    status = "WARNING";
  } else {
    status = "SAFE";
  }
}

function updateFakeSensorWorld() {
  if (simulatedNearObjectActive) {
    distanceCm += randomInt(-2, 2);
    distanceCm = clamp(distanceCm, 8, 35);
  } else {
    distanceCm += randomInt(-7, 7);
    distanceCm = clamp(distanceCm, 45, config.maxDistanceCm);
  }

  temperature += randomBetween(-0.12, 0.12);
  humidity += randomBetween(-0.3, 0.3);
  smoke += randomInt(-12, 12);
  soil += randomInt(-8, 8);

  temperature = clamp(temperature, 20, 60);
  humidity = clamp(humidity, 25, 95);
  smoke = clamp(smoke, 250, 2600);
  soil = clamp(soil, 1000, 3500);

  if (!simulatedFireActive && !simulatedNearObjectActive && Math.random() < 0.004) {
    simulatedFireActive = true;
    simulatedFireAngle = randomInt(35, 145);
    smoke = randomInt(1500, 2300);
    temperature = randomBetween(42, 54);
    setEvent(`Fake smoke/fire started near ${simulatedFireAngle} degrees`);
  }

  if (!simulatedSoilDryMode && !simulatedFireActive && !simulatedNearObjectActive && Math.random() < 0.003) {
    simulatedSoilDryMode = true;
    soil = randomInt(2700, 3300);
    setEvent("Fake soil became dry");
  }

  if (
    autoState === AutoState.AUTO_SCAN &&
    !simulatedFireActive &&
    !simulatedSoilDryMode &&
    !simulatedNearObjectActive &&
    Math.random() < 0.004
  ) {
    simulatedNearObjectActive = true;
    distanceCm = randomInt(10, 25);
    setEvent(`Fake near object detected at ${Math.round(distanceCm)}cm`);
  }

  if (simulatedFireActive) {
    const angleDiff = Math.abs(angle - simulatedFireAngle);
    const closeness = clamp(1 - angleDiff / 80, 0, 1);

    if (config.flameStrongIsLower) {
      flameAnalog = Math.round(4095 - closeness * 3300 + randomInt(-80, 80));
    } else {
      flameAnalog = Math.round(700 + closeness * 3300 + randomInt(-80, 80));
    }

    flameAnalog = clamp(flameAnalog, 0, 4095);

    flame = angleDiff < 22 && isFlameStrongEnough();

    smoke = clamp(smoke + randomInt(-20, 40), 1400, 2600);
    temperature = clamp(temperature + randomBetween(-0.2, 0.35), 40, 58);
  } else {
    flame = false;
    flameAnalog = config.flameStrongIsLower
      ? randomInt(3000, 4095)
      : randomInt(0, 900);

    if (smoke > 600) {
      smoke -= randomInt(10, 30);
    }

    if (temperature > 30) {
      temperature -= randomBetween(0.1, 0.3);
    }
  }

  flameScore = getFlameScore(flameAnalog);

  if (!simulatedSoilDryMode && soil > 1900) {
    soil -= randomInt(5, 15);
  }
}

function updateRadarScanServo() {
  const now = Date.now();

  if (now - lastScanMoveAt < config.scanIntervalMs) {
    return;
  }

  lastScanMoveAt = now;

  angle += scanDirection * config.scanStep;

  if (angle >= config.scanMaxAngle) {
    angle = config.scanMaxAngle;
    scanDirection = -1;
  }

  if (angle <= config.scanMinAngle) {
    angle = config.scanMinAngle;
    scanDirection = 1;
  }
}

function handleAutoScanState() {
  applyPump(false);
  applyBuzzer(false);

  if (status === "SAFE") {
    applyLeds(true, false, false);
  } else if (status === "WARNING") {
    applyLeds(false, true, false);
  } else {
    applyLeds(false, false, true);
  }

  if (isFireOrSmokeDetected()) {
    bestFlameScore = 0;
    bestFlameAngle = angle;
    fireSearchAngle = config.fireSearchMinAngle;
    fireAttempts = 0;

    changeAutoState(
      AutoState.FIRE_SEARCH,
      "Fake smoke/fire detected. Searching flame source."
    );
    return;
  }

  if (isSoilDry()) {
    soilAttempts = 0;

    changeAutoState(
      AutoState.SOIL_WATER_MOVE,
      "Fake soil dry. Moving to watering angle."
    );
    return;
  }

  if (isNearDanger() && canRunNearDeterrent()) {
    changeAutoState(
      AutoState.NEAR_DETERRENT,
      "Fake near object detected. Deterrent activated."
    );
    return;
  }

  if (isNearWarning()) {
    applyLeds(false, true, false);
    applyBuzzer(true);
  }

  updateRadarScanServo();
}

function handleFireSearchState() {
  const now = Date.now();

  applyPump(false);
  applyBuzzer(true);
  applyLeds(false, false, true);

  if (now - lastScanMoveAt < config.scanIntervalMs) {
    return;
  }

  lastScanMoveAt = now;

  angle = clamp(
    fireSearchAngle,
    config.fireSearchMinAngle,
    config.fireSearchMaxAngle
  );

  const score = getFlameScore(flameAnalog);

  if (score > bestFlameScore) {
    bestFlameScore = score;
    bestFlameAngle = angle;
  }

  fireSearchAngle += config.fireSearchStep;

  if (fireSearchAngle > config.fireSearchMaxAngle) {
    angle = bestFlameAngle;

    if (bestFlameScore > 0 || flame || isSmokeDanger()) {
      changeAutoState(
        AutoState.FIRE_EXTINGUISH,
        "Fake flame source locked. Extinguishing."
      );
    } else {
      changeAutoState(
        AutoState.AUTO_SCAN,
        "Fake no flame source found. Returning to radar scan."
      );
    }
  }
}

function handleFireExtinguishState() {
  const now = Date.now();

  angle = clamp(bestFlameAngle, 0, 180);

  applyLeds(false, false, true);
  applyBuzzer(true);

  const fireStillPresent = isFireOrSmokeDetected();

  if (!fireStillPresent) {
    applyPump(false);
    applyBuzzer(false);

    simulatedFireActive = false;

    changeAutoState(
      AutoState.AUTO_SCAN,
      "Fake flame appears off. Returning to radar scan."
    );
    return;
  }

  if (fireAttempts >= config.maxFireAttempts) {
    applyPump(false);
    applyBuzzer(true);

    simulatedFireActive = false;
    smoke = 900;
    temperature = 32;
    flame = false;

    changeAutoState(
      AutoState.AUTO_SCAN,
      "Fake max fire attempts reached. Returning to radar scan."
    );
    return;
  }

  if (!firePumpRunning && actionTimer === 0) {
    firePumpRunning = true;
    actionTimer = now + config.firePumpBurstMs;
    fireAttempts++;

    applyPump(true);
    setEvent("Fake fire pump burst started");
    return;
  }

  if (firePumpRunning && now >= actionTimer) {
    firePumpRunning = false;
    actionTimer = now + config.fireSettleMs;

    applyPump(false);

    smoke -= randomInt(350, 700);
    temperature -= randomBetween(4, 8);

    if (smoke < 1200 && temperature < 42) {
      simulatedFireActive = false;
      flame = false;
      flameAnalog = config.flameStrongIsLower
        ? randomInt(3200, 4095)
        : randomInt(0, 800);
    }

    setEvent("Fake fire pump paused for recheck");
    return;
  }

  if (!firePumpRunning && actionTimer !== 0 && now >= actionTimer) {
    actionTimer = 0;
  }
}

function handleSoilWaterMoveState() {
  if (isFireOrSmokeDetected()) {
    bestFlameScore = 0;
    bestFlameAngle = angle;
    fireSearchAngle = config.fireSearchMinAngle;
    fireAttempts = 0;

    changeAutoState(
      AutoState.FIRE_SEARCH,
      "Fake fire detected during soil action. Switching to fire response."
    );
    return;
  }

  angle = clamp(config.soilWaterAngle, 0, 180);

  applyPump(false);
  applyBuzzer(false);
  applyLeds(false, true, false);

  changeAutoState(
    AutoState.SOIL_WATERING,
    "Fake soil watering started"
  );
}

function handleSoilWateringState() {
  const now = Date.now();

  if (isFireOrSmokeDetected()) {
    applyPump(false);
    soilPumpRunning = false;
    actionTimer = 0;

    bestFlameScore = 0;
    bestFlameAngle = angle;
    fireSearchAngle = config.fireSearchMinAngle;
    fireAttempts = 0;

    changeAutoState(
      AutoState.FIRE_SEARCH,
      "Fake fire detected during soil watering. Switching to fire response."
    );
    return;
  }

  angle = clamp(config.soilWaterAngle, 0, 180);

  applyBuzzer(false);
  applyLeds(false, true, false);

  if (isSoilWetEnough()) {
    applyPump(false);
    soilPumpRunning = false;
    actionTimer = 0;
    simulatedSoilDryMode = false;

    changeAutoState(
      AutoState.AUTO_SCAN,
      "Fake soil moisture restored. Returning to radar scan."
    );
    return;
  }

  if (soilAttempts >= config.maxSoilAttempts) {
    applyPump(false);
    soilPumpRunning = false;
    actionTimer = 0;
    simulatedSoilDryMode = false;

    changeAutoState(
      AutoState.AUTO_SCAN,
      "Fake max soil watering attempts reached. Returning to radar scan."
    );
    return;
  }

  if (!soilPumpRunning && actionTimer === 0) {
    soilPumpRunning = true;
    actionTimer = now + config.soilPumpBurstMs;
    soilAttempts++;

    applyPump(true);
    setEvent("Fake soil pump burst started");
    return;
  }

  if (soilPumpRunning && now >= actionTimer) {
    soilPumpRunning = false;
    actionTimer = now + config.soilSettleMs;

    applyPump(false);

    soil -= randomInt(450, 800);
    soil = clamp(soil, 1000, 3500);

    setEvent("Fake soil pump paused for moisture recheck");
    return;
  }

  if (!soilPumpRunning && actionTimer !== 0 && now >= actionTimer) {
    actionTimer = 0;
  }
}

function handleNearDeterrentState() {
  const now = Date.now();

  if (isFireOrSmokeDetected()) {
    applyPump(false);
    nearPumpRunning = false;
    actionTimer = 0;

    bestFlameScore = 0;
    bestFlameAngle = angle;
    fireSearchAngle = config.fireSearchMinAngle;
    fireAttempts = 0;

    changeAutoState(
      AutoState.FIRE_SEARCH,
      "Fake fire detected during near deterrent. Switching to fire response."
    );
    return;
  }

  applyLeds(false, true, false);
  applyBuzzer(true);

  if (!nearPumpRunning && actionTimer === 0) {
    nearPumpRunning = true;
    actionTimer = now + config.nearPumpBurstMs;
    lastNearDeterrentTime = now;

    applyPump(true);
    setEvent("Fake near deterrent pump burst started");
    return;
  }

  if (nearPumpRunning && now >= actionTimer) {
    nearPumpRunning = false;
    actionTimer = 0;

    applyPump(false);
    applyBuzzer(false);

    simulatedNearObjectActive = false;
    distanceCm = randomInt(90, config.maxDistanceCm);

    changeAutoState(
      AutoState.AUTO_SCAN,
      "Fake near deterrent finished. Returning to radar scan."
    );
  }
}

function updateAutoStateMachine() {
  if (!autoMode) return;

  switch (autoState) {
    case AutoState.AUTO_SCAN:
      handleAutoScanState();
      break;

    case AutoState.FIRE_SEARCH:
      handleFireSearchState();
      break;

    case AutoState.FIRE_EXTINGUISH:
      handleFireExtinguishState();
      break;

    case AutoState.SOIL_WATER_MOVE:
      handleSoilWaterMoveState();
      break;

    case AutoState.SOIL_WATERING:
      handleSoilWateringState();
      break;

    case AutoState.NEAR_DETERRENT:
      handleNearDeterrentState();
      break;
  }
}

function updateManualSafety() {
  if (autoMode) return;

  const now = Date.now();

  if (manualLastSeen > 0 && now - manualLastSeen > config.manualControlTimeoutMs) {
    autoMode = true;
    autoState = AutoState.AUTO_SCAN;

    applyPump(false);
    applyBuzzer(false);

    setEvent("Fake manual timeout. Returned to AUTO.");
    return;
  }

  if (pump && manualPumpStopAt > 0 && now >= manualPumpStopAt) {
    applyPump(false);
    manualPumpStopAt = 0;
    setEvent("Fake manual pump stopped by safety timer");
  }
}

function handleCommand(command) {
  lastCommand = JSON.stringify(command);
  manualLastSeen = Date.now();

  console.log("[COMMAND]", command);

  const type = command.type;

  if (type === "mode") {
    autoMode = command.auto !== false;

    if (autoMode) {
      autoState = AutoState.AUTO_SCAN;
      applyPump(false);
      applyBuzzer(false);
      setEvent("Fake mode changed to AUTO");
    } else {
      applyPump(false);
      setEvent("Fake mode changed to MANUAL");
    }

    return;
  }

  if (type === "servo") {
    autoMode = false;
    angle = clamp(Number(command.angle ?? angle), 0, 180);
    setEvent(`Fake servo moved to ${angle} degrees`);
    return;
  }

  if (type === "pump") {
    autoMode = false;

    pump = Boolean(command.value);

    if (pump) {
      const ttl = clamp(Number(command.ttl ?? 3000), 300, config.maxManualPumpRunMs);
      manualPumpStopAt = Date.now() + ttl;
      setEvent(`Fake pump ON for ${ttl}ms`);
    } else {
      manualPumpStopAt = 0;
      setEvent("Fake pump OFF");
    }

    return;
  }

  if (type === "buzzer") {
    autoMode = false;
    buzzer = Boolean(command.value);
    setEvent(`Fake buzzer ${buzzer ? "ON" : "OFF"}`);
    return;
  }

  if (type === "led") {
    autoMode = false;

    green = Boolean(command.green);
    yellow = Boolean(command.yellow);
    red = Boolean(command.red);

    setEvent("Fake LED command received");
    return;
  }

  if (type === "all") {
    autoMode = command.auto === true;

    if (command.servo !== undefined) {
      angle = clamp(Number(command.servo), 0, 180);
    }

    if (command.pump !== undefined) {
      pump = Boolean(command.pump);
    }

    if (command.buzzer !== undefined) {
      buzzer = Boolean(command.buzzer);
    }

    if (command.green !== undefined) {
      green = Boolean(command.green);
    }

    if (command.yellow !== undefined) {
      yellow = Boolean(command.yellow);
    }

    if (command.red !== undefined) {
      red = Boolean(command.red);
    }

    setEvent("Fake all-control command received");
    return;
  }

  if (type === "test_fire") {
    simulatedFireActive = true;
    simulatedNearObjectActive = false;
    simulatedFireAngle = clamp(Number(command.angle ?? 75), 0, 180);
    smoke = 2200;
    temperature = 50;
    setEvent(`Fake test fire created at ${simulatedFireAngle} degrees`);
    return;
  }

  if (type === "test_soil") {
    simulatedSoilDryMode = true;
    simulatedNearObjectActive = false;
    soil = 3100;
    setEvent("Fake test dry soil created");
    return;
  }

  if (type === "test_near") {
    simulatedNearObjectActive = true;
    distanceCm = clamp(Number(command.distance ?? 15), 5, config.maxDistanceCm);
    setEvent(`Fake test near object created at ${distanceCm}cm`);
    return;
  }

  if (type === "clear") {
    simulatedFireActive = false;
    simulatedSoilDryMode = false;
    simulatedNearObjectActive = false;

    flame = false;
    smoke = 500;
    temperature = 28;
    soil = 1700;
    distanceCm = 160;

    pump = false;
    buzzer = false;
    autoState = AutoState.AUTO_SCAN;

    setEvent("Fake environment cleared");
    return;
  }

  if (type === "ping") {
    setEvent("Fake ping received");
    return;
  }

  setEvent("Fake unknown command received");
}

commandSocket.on("message", (message) => {
  try {
    const command = JSON.parse(message.toString());
    handleCommand(command);
  } catch (error) {
    console.log("Invalid command:", message.toString());
  }
});

commandSocket.on("listening", () => {
  const address = commandSocket.address();

  console.log("--------------------------------------");
  console.log("Fake ESP32 Turret Simulator");
  console.log("--------------------------------------");
  console.log(`Sending telemetry to: ${DASHBOARD_IP}:${DASHBOARD_TELEMETRY_PORT}`);
  console.log(`Listening for commands on: ${address.address}:${address.port}`);
  console.log("--------------------------------------");
  console.log("Extra fake commands supported:");
  console.log('{ "type": "test_fire", "angle": 80 }');
  console.log('{ "type": "test_soil" }');
  console.log('{ "type": "test_near", "distance": 15 }');
  console.log('{ "type": "clear" }');
  console.log("--------------------------------------");
});

commandSocket.bind(FAKE_ESP32_COMMAND_PORT);

function sendTelemetry() {
  const data = {
    type: "telemetry",
    device: "fake_environmental_turret",
    fake: true,

    millis: Date.now(),
    ip: "fake-node-simulator",

    auto: autoMode,
    auto_state: autoState,
    status,

    angle: Math.round(angle),
    distance_cm: Math.round(distanceCm),

    near_warning: isNearWarning(),
    near_danger: isNearDanger(),
    near_warning_enabled: config.nearWarningEnabled,
    near_pump_deterrent_enabled: config.nearPumpDeterrentEnabled,
    near_warning_distance_cm: config.nearWarningDistanceCm,
    near_danger_distance_cm: config.nearDangerDistanceCm,

    temperature: Number(temperature.toFixed(1)),
    humidity: Number(humidity.toFixed(1)),
    smoke: Math.round(smoke),
    soil: Math.round(soil),

    flame,
    flame_analog: Math.round(flameAnalog),
    flame_score: Math.round(flameScore),

    best_flame_angle: Math.round(bestFlameAngle),
    best_flame_score: Math.round(bestFlameScore),

    pump,
    buzzer,

    green,
    yellow,
    red,

    soil_water_angle: config.soilWaterAngle,
    fire_attempts: fireAttempts,
    soil_attempts: soilAttempts,

    event: latestEvent,
    last_command: lastCommand
  };

  const message = Buffer.from(JSON.stringify(data));

  telemetrySocket.send(
    message,
    DASHBOARD_TELEMETRY_PORT,
    DASHBOARD_IP,
    (error) => {
      if (error) {
        console.error("Telemetry send error:", error.message);
      }
    }
  );
}

setInterval(() => {
  updateFakeSensorWorld();
  updateStatus();

  if (autoMode) {
    updateAutoStateMachine();
  } else {
    updateManualSafety();
  }
}, 50);

setInterval(() => {
  sendTelemetry();
}, 100);