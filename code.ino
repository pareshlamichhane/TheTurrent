#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESP32Servo.h>

// --- Hardware Pin Definitions ---
const int servoPin = 13;  
const int trigPin = 18;     
const int echoPin = 5;      
const int flamePin = 7;   
const int relayPin = 17;    // Relay Pin (Active-Low)
const int buzzerPin = 12;   // Buzzer Pin (Active-High)
#define RGB_LED_PIN 48 

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

// --- Tracking and Locking Variables ---
enum RadarState { SWEEPING, MEASURING_FLAME, LOCKED_FLAME };
RadarState radarState = SWEEPING;

// Flame tracking variables
const int FLAME_THRESHOLD = 800; 
int maxFlameIntensity = 0;
int maxFlameAngle = 0;

// --- Timeout Variables ---
unsigned long lockStartTime = 0;
const unsigned long MAX_LOCK_TIME = 5000; // 5 seconds max lock duration

// --- Embedded HTML/JS Frontend ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-S3 Flame & Ultrasonic Radar</title>
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
        <h2>180° FLAME & DISTANCE SCANNER</h2>
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
        let currentAngle = 0;
        let flameActive = false;
        let blips = []; 

        socket.onmessage = function(event) {
            const data = JSON.parse(event.data);
            currentAngle = data.angle;
            const distance = data.distance;
            const flameIntensity = data.flame;
            
            flameActive = flameIntensity > 800; 

            let flameText = flameActive ? `<span class="danger">FLAME DETECTED (${flameIntensity})</span>` : 'CLEAR';
            stats.innerHTML = `Angle: <strong>${currentAngle}°</strong> | Distance: <strong>${distance}cm</strong> | STATUS: ${flameText}`;

            if (flameActive) {
                document.body.classList.add('flame-alert');
            } else {
                document.body.classList.remove('flame-alert');
            }

            let finalDistance = distance > maxDistance ? maxDistance : distance;

            blips.push({
                angle: currentAngle,
                distance: finalDistance, 
                isFlame: flameActive,
                opacity: 1.0
            });
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
                if (blip.opacity <= 0) {
                    blips.splice(i, 1);
                }
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

  pinMode(trigPin, OUTPUT);  
  pinMode(echoPin, INPUT);    
  pinMode(flamePin, INPUT);
  
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH); // Relay OFF for active-low
  
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);  
  
  pinMode(RGB_LED_PIN, OUTPUT);
  neopixelWrite(RGB_LED_PIN, 0, 0, 0); 
  
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
  
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    radarServo.write(currentAngle);
    
    int distance = getDistance(); 

    int rawFlame = analogRead(flamePin);
    int flameIntensity = 4095 - rawFlame; 
    
    Serial.print("Angle: "); Serial.print(currentAngle);
    Serial.print(" | Distance: "); Serial.print(distance);
    Serial.print(" | Flame: "); Serial.print(flameIntensity);
    Serial.print(" | State: "); Serial.println(radarState);

    String jsonString = "{\"angle\":" + String(currentAngle) + ", \"distance\":" + String(distance) + ", \"flame\":" + String(flameIntensity) + "}";
    webSocket.broadcastTXT(jsonString);

    switch (radarState) {
      
      case SWEEPING:
        neopixelWrite(RGB_LED_PIN, 0, 64, 0); // 🟢 GREEN Light
        digitalWrite(relayPin, HIGH);         // Relay OFF 
        digitalWrite(buzzerPin, LOW);         // Buzzer OFF
        
        if (flameIntensity > FLAME_THRESHOLD) {
          radarState = MEASURING_FLAME;
          maxFlameIntensity = flameIntensity;
          maxFlameAngle = currentAngle;
        } 
        advanceSweep();
        break;

      case MEASURING_FLAME:
        neopixelWrite(RGB_LED_PIN, 0, 0, 64); // 🔵 BLUE Light
        digitalWrite(relayPin, HIGH);         
        digitalWrite(buzzerPin, LOW);         
        
        if (flameIntensity > maxFlameIntensity) {
          maxFlameIntensity = flameIntensity;
          maxFlameAngle = currentAngle;
        }
        if (flameIntensity <= FLAME_THRESHOLD || currentAngle == 0 || currentAngle == 180) {
          currentAngle = maxFlameAngle; 
          radarState = LOCKED_FLAME;
          lockStartTime = millis();           // Capture time when lock begins
        } else {
          advanceSweep();
        }
        break;

      case LOCKED_FLAME:
        neopixelWrite(RGB_LED_PIN, 64, 0, 0); // 🔴 RED Light
        digitalWrite(relayPin, LOW);          // Relay ON (Active-Low)
        digitalWrite(buzzerPin, HIGH);         // Buzzer ON
        
        // Break lock if flame goes away OR if 5 seconds have passed
        if (flameIntensity <= FLAME_THRESHOLD || (millis() - lockStartTime >= MAX_LOCK_TIME)) {
          // Reset tracking data before searching again
          maxFlameIntensity = 0; 
          radarState = SWEEPING;
        }
        break;
    }
  }
}
