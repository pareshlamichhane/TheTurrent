#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESP32Servo.h>

// --- Hardware Pin Definitions ---
const int servoPin = 18;  
const int trigPin = 8;     
const int echoPin = 16;    
const int flamePin = 7;   
const int buzzerPin = 6;  

// Future Implementation Pins
// const int relayPin = 17;  // Reserved for relay
// const int smokePin = 4;   // Reserved for MQ smoke sensor

Servo radarServo;

// --- Wi-Fi Hotspot Credentials ---
const char* ssid = "ESP32_Radar";
const char* password = "password123";

// --- Server & WebSockets ---
WebServer server(80);
WebSocketsServer webSocket(81);

// --- Radar Sweep Variables ---
unsigned long previousMillis = 0;
const long interval = 40; // 40ms sweep delay
int currentAngle = 0;
bool sweepingForward = true;

// --- Buzzer Timer Variables ---
unsigned long previousBuzzerMillis = 0;
int buzzerInterval = 0;   // 0 = Off, 100 = Fast beep, 500 = Slow beep
bool buzzerState = HIGH;  // DEFAULT TO HIGH (Active-Low logic = OFF)

// --- Tracking and Locking Variables ---
enum RadarState { SWEEPING, MEASURING_OBJECT, MEASURING_FLAME, LOCKED_OBJECT, LOCKED_FLAME };
RadarState radarState = SWEEPING;

// Object tracking variables
int objectStartAngle = 0;
int lockedAngle = 0;

// Flame tracking variables
const int FLAME_THRESHOLD = 800; 
int maxFlameIntensity = 0;
int maxFlameAngle = 0;

// --- Embedded HTML/JS Frontend ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-S3 Security Radar</title>
    <style>
        body {
            background-color: #050505;
            color: #00ff33;
            font-family: 'Courier New', Courier, monospace;
            margin: 0;
            overflow: hidden;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            height: 100vh;
            transition: background-color 0.3s;
        }
        .flame-alert {
            background-color: #330000 !important;
            color: #ff3333 !important;
        }
        #radar-container { position: relative; text-align: center; }
        canvas {
            background-color: #000;
            border: 2px solid #005511;
            border-radius: 400px 400px 0 0; 
            box-shadow: 0 0 30px rgba(0, 255, 50, 0.15);
            transition: box-shadow 0.3s, border-color 0.3s;
        }
        .flame-alert canvas {
            border-color: #ff0000;
            box-shadow: 0 0 40px rgba(255, 0, 0, 0.5);
        }
        #stats { margin-top: 15px; font-size: 18px; letter-spacing: 1px; }
        .danger { color: #ff3333; font-weight: bold; animation: blink 1s infinite; }
        @keyframes blink { 50% { opacity: 0.5; } }
    </style>
</head>
<body>
    <div id="radar-container">
        <h2>180° SECURITY SCANNER</h2>
        <canvas id="radarCanvas" width="800" height="400"></canvas>
        <div id="stats">Angle: 0° | Distance: Calculating... | System: Booting...</div>
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
        const smoothingFactor = 0.3; 

        let currentAngle = 0;
        let rawDistance = maxDistance;
        let filteredDistance = maxDistance; 
        let flameActive = false;
        let blips = []; 

        socket.onmessage = function(event) {
            const data = JSON.parse(event.data);
            currentAngle = data.angle;
            rawDistance = data.distance;
            const flameIntensity = data.flame;
            
            flameActive = flameIntensity > 800; 

            if (rawDistance >= maxDistance || rawDistance <= 2) {
                rawDistance = maxDistance;
            }

            filteredDistance = (rawDistance * smoothingFactor) + (filteredDistance * (1 - smoothingFactor));

            let flameText = flameActive ? `<span class="danger">FLAME DETECTED (${flameIntensity})</span>` : 'CLEAR';
            stats.innerHTML = `Angle: <strong>${currentAngle}°</strong> | Distance: <strong>${rawDistance === maxDistance ? 'MAX' : Math.round(filteredDistance) + ' cm'}</strong> | STATUS: ${flameText}`;

            if (flameActive) {
                document.body.classList.add('flame-alert');
            } else {
                document.body.classList.remove('flame-alert');
            }

            if (filteredDistance < maxDistance || flameActive) {
                blips.push({
                    angle: currentAngle,
                    distance: flameActive ? maxDistance / 2 : filteredDistance, 
                    isFlame: flameActive,
                    opacity: 1.0
                });
            }
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
            ctx.moveTo(centerX, centerY);
            ctx.lineTo(centerX, centerY - radarRadius);
            ctx.stroke();

            for (let i = blips.length - 1; i >= 0; i--) {
                let blip = blips[i];
                let angleRad = blip.angle * Math.PI / 180;
                let visualRadius = (blip.distance / maxDistance) * radarRadius;

                let blipX = centerX + visualRadius * Math.cos(angleRad);
                let blipY = centerY - visualRadius * Math.sin(angleRad);

                ctx.fillStyle = blip.isFlame ? `rgba(255, 165, 0, ${blip.opacity})` : `rgba(255, 0, 0, ${blip.opacity})`;
                ctx.beginPath();
                ctx.arc(blipX, blipY, blip.isFlame ? 12 : 6, 0, 2 * Math.PI);
                ctx.fill();

                blip.opacity -= 0.01;
                if (blip.opacity <= 0) {
                    blips.splice(i, 1);
                }
            }

            let sweepRad = currentAngle * Math.PI / 180;
            let currentVisualRadius = (filteredDistance / maxDistance) * radarRadius;
            
            let endX = centerX + currentVisualRadius * Math.cos(sweepRad);
            let endY = centerY - currentVisualRadius * Math.sin(sweepRad);

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

void setup() {
  Serial.begin(115200);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(flamePin, INPUT);
  pinMode(buzzerPin, OUTPUT);
  
  // FIX: Active-low buzzer requires HIGH to stay silent on boot
  digitalWrite(buzzerPin, HIGH); 
  
  // Future setups
  // pinMode(relayPin, OUTPUT);
  // pinMode(smokePin, INPUT);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
  radarServo.setPeriodHertz(50);
  radarServo.attach(servoPin, 700, 2200); 
  radarServo.write(0);

  Serial.println("Setting up Access Point...");
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  server.on("/", []() {
    server.send(200, "text/html", index_html);
  });
  server.begin();
  
  webSocket.begin();
}

// Helper function to advance the servo angle
void advanceSweep() {
  if (sweepingForward) {
    currentAngle++;
    if (currentAngle >= 180) sweepingForward = false;
  } else {
    currentAngle--;
    if (currentAngle <= 0) sweepingForward = true;
  }
}

void loop() {
  server.handleClient();
  webSocket.loop();

  unsigned long currentMillis = millis();

  // --- NON-BLOCKING BUZZER LOGIC ---
  if (buzzerInterval > 0) {
    if (currentMillis - previousBuzzerMillis >= buzzerInterval) {
      previousBuzzerMillis = currentMillis;
      buzzerState = !buzzerState; // Toggle state
      digitalWrite(buzzerPin, buzzerState);
    }
  } else {
    // FIX: Force OFF (HIGH) if interval is 0
    digitalWrite(buzzerPin, HIGH); 
    buzzerState = HIGH;
  }
  
  // --- RADAR SWEEP & SENSOR LOGIC ---
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    radarServo.write(currentAngle);
    
    int distance = getDistance();
    if (distance > 150 || distance == 0) distance = 150; 

    int rawFlame = analogRead(flamePin);
    int flameIntensity = 4095 - rawFlame; 
    
    // --- SERIAL MONITOR OUTPUT ---
    Serial.print("Angle: "); Serial.print(currentAngle);
    Serial.print(" | Dist: "); Serial.print(distance); Serial.print("cm");
    Serial.print(" | Flame: "); Serial.print(flameIntensity);
    // Because the buzzer is active-LOW, LOW = ON and HIGH = OFF
    Serial.print(" | Buzzer: "); Serial.println(buzzerState == LOW ? "ON" : "OFF");

    String jsonString = "{\"angle\":" + String(currentAngle) + ", \"distance\":" + String(distance) + ", \"flame\":" + String(flameIntensity) + "}";
    webSocket.broadcastTXT(jsonString);

    // --- State Machine Logic ---
    switch (radarState) {
      
      case SWEEPING:
        buzzerInterval = 0; // Silent while sweeping
        if (flameIntensity > FLAME_THRESHOLD) {
          radarState = MEASURING_FLAME;
          maxFlameIntensity = flameIntensity;
          maxFlameAngle = currentAngle;
        } 
        else if (distance < 15) {
          radarState = MEASURING_OBJECT;
          objectStartAngle = currentAngle;
        }
        advanceSweep();
        break;

      case MEASURING_OBJECT:
        buzzerInterval = 0; 
        if (flameIntensity > FLAME_THRESHOLD) {
          radarState = MEASURING_FLAME;
          maxFlameIntensity = flameIntensity;
          maxFlameAngle = currentAngle;
        } 
        else if (distance >= 15 || currentAngle == 0 || currentAngle == 180) {
          lockedAngle = (objectStartAngle + currentAngle) / 2;
          currentAngle = lockedAngle; 
          radarState = LOCKED_OBJECT;
        } else {
          advanceSweep();
        }
        break;

      case MEASURING_FLAME:
        buzzerInterval = 100; // FAST BEEP for Flame Warning
        if (flameIntensity > maxFlameIntensity) {
          maxFlameIntensity = flameIntensity;
          maxFlameAngle = currentAngle;
        }
        if (flameIntensity <= FLAME_THRESHOLD || currentAngle == 0 || currentAngle == 180) {
          currentAngle = maxFlameAngle; 
          radarState = LOCKED_FLAME;
        } else {
          advanceSweep();
        }
        break;

      case LOCKED_OBJECT:
        buzzerInterval = 500; // SLOW BEEP for Intruder Alert
        if (flameIntensity > FLAME_THRESHOLD) {
          radarState = MEASURING_FLAME;
          maxFlameIntensity = flameIntensity;
          maxFlameAngle = currentAngle;
        } 
        else if (distance > 20) {
          radarState = SWEEPING;
        }
        break;

      case LOCKED_FLAME:
        buzzerInterval = 100; // FAST BEEP for Flame Lock
        if (flameIntensity <= FLAME_THRESHOLD) {
          radarState = SWEEPING;
        }
        break;
    }
  }
}

int getDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 30000); 
  return duration * 0.034 / 2;
}
