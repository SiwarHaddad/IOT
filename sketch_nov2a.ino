#include <WiFi.h>
#include <FirebaseESP32.h>
#include <TinyGPSPlus.h>
#include <DHT.h>
#include <time.h>

// =============== CONFIG ===============
const char* ssid     = "Siwar's A55";
const char* password = "7ahrxj76uuyz6dh";

#define FIREBASE_HOST "projet-iot-f4017-default-rtdb.europe-west1.firebasedatabase.app"
#define FIREBASE_AUTH "aOlUd6WrS9qxwhG9ocVyFgxgwZoqc51Y1NRxhaPC"

#define DHTPIN   19
#define DHTTYPE  DHT11

#define LED_RED     27
#define LED_ORANGE  14
#define LED_BLUE    12

// =============== OBJECTS ===============
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
DHT dht(DHTPIN, DHTTYPE);

// Stats
float sumT = 0, sumH = 0;
int count  = 0;

// =============== TIME ===============
void initTime() {
  setenv("TZ", "CET-1", 1);  
  tzset();
  configTime(0, 0,
             "pool.ntp.org",
             "time.google.com",
             "time.cloudflare.com");

  struct tm tmnow;
  for (int i = 0; i < 30; ++i) {
    if (getLocalTime(&tmnow, 500)) break;
  }
}

bool ntpTimeUsable() {
  time_t nowEpoch = time(nullptr);
  return nowEpoch > 1700000000; 
}

bool gpsTimeUsable() {
  if (!gps.date.isValid() || !gps.time.isValid()) return false;
  if (gps.date.year() < 2023) return false;                 
  if (gps.time.age() > 5000 || gps.date.age() > 5000) return false; // too old
  return true;
}

String getTimestamp() {
  if (ntpTimeUsable()) {
    time_t nowEpoch = time(nullptr);
    struct tm t;
    gmtime_r(&nowEpoch, &t);
    char buf[25];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return String(buf);
  }
  if (gpsTimeUsable()) {
    char buf[25];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
    return String(buf);
  }
  return String("1970-01-01T00:00:00Z");
}

// =============== LEDS ===============
void updateLEDs(float temp, float hum, bool zoneRisque) {
  if (temp > 60 || hum > 80 || zoneRisque) {
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_ORANGE, LOW);
    digitalWrite(LED_BLUE, LOW);
  } else if (temp > 40 || hum > 70) {
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_ORANGE, HIGH);
    digitalWrite(LED_BLUE, LOW);
  } else {
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_ORANGE, LOW);
    digitalWrite(LED_BLUE, HIGH);
  }
}

// =============== STATS ===============
void sendStats() {
  if (count == 0) return;
  float avgT = sumT / count;
  float avgH = sumH / count;

  Firebase.setFloat(fbdo, "/stats/avgTemp", avgT);
  Firebase.setFloat(fbdo, "/stats/avgHum",  avgH);

  if (gps.location.isValid()) {
    String zone = String(gps.location.lat(), 3) + "_" + String(gps.location.lng(), 3);
    int incidents = 0;
    if (Firebase.getInt(fbdo, "/stats/zonesRisque/" + zone)) {
      incidents = fbdo.to<int>();
    }
    Firebase.setInt(fbdo, "/stats/zonesRisque/" + zone, incidents + 1);
  }

  sumT = 0; sumH = 0; count = 0;
}

// =============== SETUP ===============
void setup() {
  Serial.begin(115200);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_ORANGE, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, HIGH);

  pinMode(DHTPIN, INPUT_PULLUP);
  dht.begin();

  gpsSerial.begin(9600, SERIAL_8N1, 17, 16);

  WiFi.begin(ssid, password);
  Serial.print("Connexion WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connecté ! IP: " + WiFi.localIP().toString());

  initTime();

  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.reconnectWiFi(true);
  Firebase.begin(&config, &auth);

  Serial.println("Firebase connecté !");
}

// =============== LOOP ===============
void loop() {
  static unsigned long lastSend = 0;

  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) {
    delay(500);
    h = dht.readHumidity();
    t = dht.readTemperature();
  }

  if (!isnan(t)) sumT += t;
  if (!isnan(h)) sumH += h;
  count++;

  if (millis() - lastSend >= 1000) {
    lastSend = millis();

    FirebaseJson json;

    if (gps.location.isValid()) {
      json.set("latitude",  String(gps.location.lat(), 6));
      json.set("longitude", String(gps.location.lng(), 6));
      json.set("altitude",  gps.altitude.meters());
    } else {
      json.set("latitude",  "N/A");
      json.set("longitude", "N/A");
      json.set("altitude",  -1);
    }

    json.set("temperature", isnan(t) ? -1 : t);
    json.set("humidity",    isnan(h) ? -1 : h);
    json.set("timestamp",   getTimestamp()); // ISO-8601 UTC

    if (Firebase.pushJSON(fbdo, "/historique", json)) {
      Serial.println("Données envoyées");
    } else {
      Serial.println("Échec : " + fbdo.errorReason());
    }

    bool zoneRisque = false;
    if (gps.location.isValid()) {
      String zone = String(gps.location.lat(), 3) + "_" + String(gps.location.lng(), 3);
      if (Firebase.getInt(fbdo, "/stats/zonesRisque/" + zone)) {
        zoneRisque = fbdo.to<int>() > 2;
      }
    }
    updateLEDs(t, h, zoneRisque);

    if (count >= 10) sendStats();

    Serial.printf("T: %.1f°C | H: %.1f%% | Alt: %.1fm | Time: %s\n",
                  t, h, gps.altitude.meters(), getTimestamp().c_str());
  }

  delay(10);
}