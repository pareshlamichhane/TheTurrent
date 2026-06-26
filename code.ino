#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESP32Servo.h>

// --- Hardware Pin Definitions ---
const int servoPin = 13;  
const int trigPin = 18;     
const int echoPin = 5;      
const int flamePin = 7;   
const int relayPin = 17;    
const int buzzerPin = 12;   
#define RGB_LED_PIN 48 

// --- CALIBRATION SETTINGS ---
int NOZZLE_OFFSET = 10; 
int SPRAY_WIDTH = 15; // Sprays 15 degrees left and right of the fire's center

// --- DIGITAL LOGIC DEFINITIONS ---
#define BUZZER_ON LOW    
#define BUZZER_OFF HIGH  
#define FLAME_DETECTED LOW   // Change to HIGH if your digital sensor works the other way

Servo radarServo;

// --- Wi-Fi Hotspot Credentials ---
const char* ssid = "ESP32_Radar";
const char* password = "password123";

// --- Server & WebSockets ---
WebServer server(80);
WebSocketsServer webSocket(81);

// --- Radar Sweep Variables ---
unsigned long previousMillis = 0;
const long interval = 40; 
int currentAngle = 0;
bool sweepingForward = true;

// --- Tracking and Locking Variables ---
enum RadarState { SWEEPING, FINDING_EDGE, SECTOR_SPRAY };
RadarState radarState = SWEEPING;

// --- Operating Modes ---
enum SystemMode { AUTO_WITH_BUZZER, AUTO_WITHOUT_BUZZER, MANUAL };
SystemMode currentMode = AUTO_WITHOUT_BUZZER; 

int fireStartAngle = 0;
int sprayCenter = 0;
bool sprayForward = true;

unsigned long lockStartTime = 0;
const unsigned long MAX_LOCK_TIME = 6000; // Sprays for 6 seconds

// --- Melodious Buzzer Rhythm Variables ---
bool triggerBuzzer = false;
bool lastTriggerBuzzer = false;
unsigned long lastBuzzerMillis = 0;
int buzzerStep = 0;
unsigned long buzzerKeepAlive = 0; 

const int rhythmPattern[] = {100, 100, 100, 100, 300, 400}; 
const int rhythmSteps = sizeof(rhythmPattern) / sizeof(rhythmPattern[0]);

// --- Embedded HTML/JS Frontend ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-S3 Digital Fire Radar</title>
    <style>
        body {
            background-color: #050505; color: #00ff33;
            font-family: 'Courier New', Courier, monospace; margin: 0;
            overflow: hidden; display: flex; flex-direction: column;
            align-items: center; justify-content: center; height: 100vh;
            transition: background-color 0.3s;
        }
        .flame-alert { background-color: #330000 !important; color: #ff3333 !important; }
        #radar-container { position: relative; text-align: center; }
        canvas {
            background-color: #000; border: 2px solid #005511;
            border-radius: 400px 400px 0 0; box-shadow: 0 0 30px rgba(0, 255, 50, 0.15);
            transition: box-shadow 0.3s, border-color 0.3s;
        }
        .flame-alert canvas { border-color: #ff0000; box-shadow: 0 0 40px rgba(255, 0, 0, 0.5); }
        #stats { margin-top: 15px; font-size: 18px; letter-spacing: 1px; }
        .danger { color: #ff3333; font-weight: bold; animation: blink 1s infinite; }
        @keyframes blink { 50% { opacity: 0.5; } }
    </style>
</head>
<body>
    <div id="radar-container">
        <h2>180° SECTOR SPRAY RADAR</h2>
        <canvas id="radarCanvas" width="800" height="400"></canvas>
        <div id="stats">Angle: 0° | Distance: 0cm | Status: Scanning...</div>
    </div>
    <script>
        const socket = new WebSocket('ws://' + window.location.hostname + ':81/');
        const canvas = document.getElementById('radarCanvas');
        const ctx = canvas.getContext('2d');
        const stats = document.getElementById('stats');

        const centerX = canvas.width / 2;    
        const centerY = canvas.height - 10;  
        const radarRadius = canvas.height - 30; 
        
        const maxDistance = 150; 
        let currentAngle = 0; let flameActive = false; let blips = []; 

        socket.onmessage = function(event) {
            const data = JSON.parse(event.data);
            currentAngle = data.angle;
            const distance = data.distance;
            
            flameActive = data.flame === 1; 

            let flameText = flameActive ? `<span class="danger">FIRE DETECTED!</span>` : 'CLEAR';
            stats.innerHTML = `Angle: <strong>${currentAngle}°</strong> | Distance: <strong>${distance}cm</strong> | STATUS: ${flameText}`;

            if (flameActive) document.body.classList.add('flame-alert');
            else document.body.classList.remove('flame-alert');

            let finalDistance = distance > maxDistance ? maxDistance : distance;
            blips.push({ angle: currentAngle, distance: finalDistance, isFlame: flameActive, opacity: 1.0 });
        };

        function drawRadar() {
            ctx.fillStyle = flameActive ? 'rgba(50, 0, 0, 0.1)' : 'rgba(0, 0, 0, 0.08)';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            ctx.strokeStyle = flameActive ? 'rgba(255, 0, 0, 0.3)' : 'rgba(0, 100, 0, 0.3)';
            ctx.lineWidth = 1;
            
            for (let r = 1; r <= 4; r++) {
                ctx.beginPath();
                ctx.arc(centerX, centerY, (radarRadius / 4) * r, Math.PI, 2 * Math.PI);
                ctx.stroke();
            }

            ctx.beginPath();
            ctx.moveTo(centerX - radarRadius, centerY);
            ctx.lineTo(centerX + radarRadius, centerY);
            ctx.stroke();

            for (let i = blips.length - 1; i >= 0; i--) {
                let blip = blips[i];
                let angleRad = blip.angle * Math.PI / 180;
                let visualRadius = (blip.distance / maxDistance) * radarRadius;
                let blipX = centerX + visualRadius * Math.cos(angleRad);
                let blipY = centerY - visualRadius * Math.sin(angleRad);

                ctx.fillStyle = blip.isFlame ? `rgba(255, 165, 0, ${blip.opacity})` : `rgba(0, 200, 50, ${blip.opacity * 0.3})`;
                ctx.beginPath();
                ctx.arc(blipX, blipY, blip.isFlame ? 12 : 5, 0, 2 * Math.PI);
                ctx.fill();

                blip.opacity -= 0.01;
                if (blip.opacity <= 0) blips.splice(i, 1);
            }

            let sweepRad = currentAngle * Math.PI / 180;
            let endX = centerX + radarRadius * Math.cos(sweepRad);
            let endY = centerY - radarRadius * Math.sin(sweepRad);

            ctx.strokeStyle = flameActive ? 'rgba(255, 0, 0, 0.8)' : 'rgba(0, 255, 50, 0.8)';
            ctx.lineWidth = 3;
            ctx.beginPath();
            ctx.moveTo(centerX, centerY);
            ctx.lineTo(endX, endY);
            ctx.stroke();

            requestAnimationFrame(drawRadar);
        }
        drawRadar();
    </script>
</body>
</html>
)rawliteral";

int getDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000); 
  if (duration == 0) return 150;                
  return duration * 0.034 / 2;
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nSystem Booting... (DIGITAL SECTOR SPRAY MODE)");
  
  pinMode(trigPin, OUTPUT);  
  pinMode(echoPin, INPUT);    
  pinMode(flamePin, INPUT_PULLUP); // Using pullup to keep digital signal stable
  
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH); 
  
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, BUZZER_OFF);  
  
  pinMode(RGB_LED_PIN, OUTPUT);
  neopixelWrite(RGB_LED_PIN, 0, 0, 0); 
  
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
  radarServo.setPeriodHertz(50);
  radarServo.attach(servoPin, 700, 2200); 
  radarServo.write(0);

  WiFi.softAP(ssid, password);
  server.on("/", []() { server.send(200, "text/html", index_html); });
  server.begin();
  webSocket.begin();
}

void advanceSweep() {
  if (sweepingForward) {
    currentAngle++;
    if (currentAngle >= 180) sweepingForward = false;
  } else {
    currentAngle--;
    if (currentAngle <= 0) sweepingForward = true;
  }
}

void handleBuzzerRhythm() {
  if (!triggerBuzzer) {
    digitalWrite(buzzerPin, BUZZER_OFF);
    buzzerStep = 0;
    lastTriggerBuzzer = false;
    return;
  }
  
  if (!lastTriggerBuzzer) {
    digitalWrite(buzzerPin, BUZZER_ON);
    lastBuzzerMillis = millis();
    buzzerStep = 0;
    lastTriggerBuzzer = true;
  }
  
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastBuzzerMillis >= rhythmPattern[buzzerStep]) {
    lastBuzzerMillis = currentMillis;
    buzzerStep++;
    if (buzzerStep >= rhythmSteps) buzzerStep = 0; 
    if (buzzerStep % 2 == 0) digitalWrite(buzzerPin, BUZZER_ON);
    else digitalWrite(buzzerPin, BUZZER_OFF);
  }
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim(); cmd.toLowerCase();
    
    if (cmd == "mode:auto") currentMode = AUTO_WITH_BUZZER;
    else if (cmd == "mode:auto_silent" || cmd == "mode:auto:silent") currentMode = AUTO_WITHOUT_BUZZER;
    else if (cmd == "mode:manual") {
      currentMode = MANUAL;
      digitalWrite(relayPin, HIGH); 
      triggerBuzzer = false;
      neopixelWrite(RGB_LED_PIN, 64, 0, 64); 
    } 
    else if (currentMode == MANUAL) {
      if (cmd.startsWith("angle:")) {
        int ang = cmd.substring(6).toInt();
        if (ang >= 0 && ang <= 180) radarServo.write(ang);
      } 
      else if (cmd == "pump:on") digitalWrite(relayPin, LOW);
      else if (cmd == "pump:off") digitalWrite(relayPin, HIGH);
      else if (cmd == "buzzer:on") triggerBuzzer = true;
      else if (cmd == "buzzer:off") triggerBuzzer = false;
    }
  }
}

void loop() {
  server.handleClient();
  webSocket.loop();
  
  handleSerialCommands();
  handleBuzzerRhythm();

  unsigned long currentMillis = millis();
  
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    
    int distance = getDistance(); 
    bool isFlame = (digitalRead(flamePin) == FLAME_DETECTED);
    int jsonFlameStatus = isFlame ? 1 : 0; 
    
    String jsonString = "{\"angle\":" + String(currentAngle) + ", \"distance\":" + String(distance) + ", \"flame\":" + String(jsonFlameStatus) + "}";
    webSocket.broadcastTXT(jsonString);

    if (currentMode == AUTO_WITH_BUZZER) {
      if (isFlame) {
        triggerBuzzer = true;
        buzzerKeepAlive = currentMillis;
      } 
      else if (currentMillis - buzzerKeepAlive > 1500) triggerBuzzer = false;
    } else if (currentMode == AUTO_WITHOUT_BUZZER) {
      triggerBuzzer = false;
    }

    if (currentMode == AUTO_WITH_BUZZER || currentMode == AUTO_WITHOUT_BUZZER) {
      switch (radarState) {
        
        case SWEEPING:
          neopixelWrite(RGB_LED_PIN, 0, 64, 0); 
          digitalWrite(relayPin, HIGH);          
          
          if (isFlame) {
            radarState = FINDING_EDGE;
            fireStartAngle = currentAngle;
          } 
          
          radarServo.write(currentAngle);
          advanceSweep();
          break;

        case FINDING_EDGE:
          neopixelWrite(RGB_LED_PIN, 0, 0, 64); 
          digitalWrite(relayPin, HIGH);         
          
          // If flame signal drops, OR we hit the edge of the radar
          if (!isFlame || currentAngle == 0 || currentAngle == 180) {
            int fireEndAngle = currentAngle;
            int actualCenter = (fireStartAngle + fireEndAngle) / 2;
            
            sprayCenter = actualCenter + NOZZLE_OFFSET;
            if (sprayCenter < 0) sprayCenter = 0;
            if (sprayCenter > 180) sprayCenter = 180;
            
            radarState = SECTOR_SPRAY;
            lockStartTime = millis();           
            currentAngle = sprayCenter; 
          } else {
            advanceSweep();
          }
          radarServo.write(currentAngle);
          break;

        case SECTOR_SPRAY:
          neopixelWrite(RGB_LED_PIN, 64, 0, 0); 
          digitalWrite(relayPin, LOW); // Blast the water!         
          
          // Sprinkler Sweep Logic
          if (sprayForward) {
            currentAngle++;
            if (currentAngle >= sprayCenter + SPRAY_WIDTH || currentAngle >= 180) sprayForward = false;
          } else {
            currentAngle--;
            if (currentAngle <= sprayCenter - SPRAY_WIDTH || currentAngle <= 0) sprayForward = true;
          }
          radarServo.write(currentAngle);

          // Stop spraying after time limit
          if (millis() - lockStartTime >= MAX_LOCK_TIME) {
            radarState = SWEEPING;
          }
          break;
      }
    } 
  }
}
