// =================== LIBRARIES ===================
#include <ESP8266WiFi.h>
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h> // Required for both MQTT and HTTP

// =================== CONFIGURATION ===================
// --- WiFi Network ---
const char* WLAN_SSID = "redmi";
const char* WLAN_PASS = "12345679";

// --- Adafruit IO Setup ---
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    "Pankaj1011" // <-- FILL THIS IN
#define AIO_KEY         "aio_Fifh43IpAG5UG3a0nmyP3PoIYxco"      // <-- FILL THIS IN

// --- Local Python Server Details ---
const String serverName = "http://192.168.81.136:5000/alert";

// --- Hardware Pins ---
const int pirSensorPin = D5;      // GPIO14 - PIR Motion Sensor
const int reedSwitchPin = D6;     // GPIO12 - Reed Switch (Door)
const int buzzerPin = D1;         // GPIO5  - Buzzer
const int armLedPin = D3;         // GPIO0  - LED to show if system is ARMED

// --- Sensor Settings ---
const bool REED_SWITCH_IS_NO = true;  // Set to 'false' if using a Normally Closed (NC) switch

// =================== STATE & TIMING VARIABLES ===================
bool alarmIsActive = false;
unsigned long alarmStartTime = 0;
const long alarmDuration = 5000;

unsigned long doorLastTriggerTime = 0;
unsigned long pirLastTriggerTime = 0;
const long doorCooldown = 15000;
const long pirCooldown = 5000;

bool isSystemArmed = false; // This is controlled by your Adafruit IO dashboard

// =================== CLIENT & FEED SETUP ===================
// Create an ESP8266 WiFiClient class for MQTT
WiFiClient client;

// Setup the MQTT client class
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// --- Adafruit IO Feeds ---
// Feed for publishing alerts (Sensor -> Cloud)
Adafruit_MQTT_Publish doorAlert = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/door-alert");
Adafruit_MQTT_Publish motionAlert = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/motion-alert");

// Feed for subscribing to commands (Cloud -> Sensor)
Adafruit_MQTT_Subscribe systemArm = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/system-arm");

// ==========================================================

void setup() {
  Serial.begin(115200);

  // Initialize hardware pins
  pinMode(pirSensorPin, INPUT);
  pinMode(reedSwitchPin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  pinMode(armLedPin, OUTPUT);
  
  digitalWrite(buzzerPin, LOW);
  digitalWrite(armLedPin, LOW);
  
  // Connect to WiFi
  Serial.print("🔌 Connecting to WiFi");
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ WiFi connected!");
  Serial.print("📡 IP Address: ");
  Serial.println(WiFi.localIP());

  // Setup MQTT subscription
  mqtt.subscribe(&systemArm);
}

// --- THIS IS YOUR ORIGINAL FUNCTION, UNCHANGED ---
void sendAlertToLocalServer(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient http_client; // Use a separate client for this HTTP request
    HTTPClient http;

    http.begin(http_client, serverName);
    http.addHeader("Content-Type", "application/json");
    String payload = "{\"message\": \"" + message + "\"}";
    
    // Send the request
    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0) {
      Serial.printf("✅ Local Alert '%s' sent! HTTP Code: %d\n", message.c_str(), httpResponseCode);
    } else {
      Serial.printf("❌ Error sending local alert. Code: %d\n", httpResponseCode);
    }

    http.end();
  } else {
    Serial.println("❌ WiFi not connected! Cannot send local alert.");
  }
}

// Function to connect and reconnect as necessary to the MQTT server.
void MQTT_connect() {
  int8_t ret;
  if (mqtt.connected()) { return; } // Stop if already connected.

  Serial.print("Connecting to MQTT... ");
  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(5000);
       retries--;
       if (retries == 0) { while (1); } // Wait for WDT reset
  }
  Serial.println("MQTT Connected!");
}


void loop() {
  // 1. Ensure connection to Adafruit IO
  MQTT_connect();

  unsigned long currentTime = millis();

  // 2. Listen for commands from Adafruit IO (e.g., arm/disarm)
  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(5000))) {
    if (subscription == &systemArm) {
      Serial.print(F("Dashboard command: "));
      Serial.println((char *)systemArm.lastread);
      
      if (strcmp((char *)systemArm.lastread, "1") == 0) {
        isSystemArmed = true;
        digitalWrite(armLedPin, HIGH);
        Serial.println("System ARMED");
      } else {
        isSystemArmed = false;
        digitalWrite(armLedPin, LOW);
        digitalWrite(buzzerPin, LOW);
        alarmIsActive = false;
        Serial.println("System DISARMED");
      }
    }
  }

  // 3. Handle local buzzer duration
  if (alarmIsActive && (currentTime - alarmStartTime >= alarmDuration)) {
    Serial.println("🔇 Buzzer OFF (5-second duration complete).");
    digitalWrite(buzzerPin, LOW);
    alarmIsActive = false;
  }

  // 4. Check sensors ONLY if the system is armed
  if (isSystemArmed) {
    
    // --- Check for Door Intrusion (High Priority) ---
    bool doorOpened = REED_SWITCH_IS_NO ? (digitalRead(reedSwitchPin) == LOW) : (digitalRead(reedSwitchPin) == HIGH);
    
    if (doorOpened && !alarmIsActive && (currentTime - doorLastTriggerTime >= doorCooldown)) {
      String alertMessage = "⚠️ INTRUSION DETECTED! The door was opened!";
      Serial.println(alertMessage);
      
      alarmIsActive = true;
      alarmStartTime = currentTime;
      doorLastTriggerTime = currentTime; 

      Serial.println("🔊 Activating buzzer for 5 seconds!");
      digitalWrite(buzzerPin, HIGH);
      
      // --- SEND ALERTS TO BOTH SERVICES ---
      doorAlert.publish(alertMessage.c_str());   // Send to Adafruit IO
      sendAlertToLocalServer(alertMessage);      // Send to your Python Server
    }

    // --- Check for Motion (Lower Priority) ---
    if (!alarmIsActive && digitalRead(pirSensorPin) == HIGH && (currentTime - pirLastTriggerTime >= pirCooldown)) {
      String alertMessage = "👀 Motion Detected near the door!";
      Serial.println(alertMessage);
      
      pirLastTriggerTime = currentTime;
      
      // --- SEND ALERTS TO BOTH SERVICES ---
      motionAlert.publish(alertMessage.c_str()); // Send to Adafruit IO
      sendAlertToLocalServer(alertMessage);      // Send to your Python Server
    }
  }

  // 5. Ping the server to keep the MQTT connection alive
  if(! mqtt.ping()) {
    mqtt.disconnect();
  }
}
