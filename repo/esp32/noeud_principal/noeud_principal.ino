/*
 * =====================================================================
 *  NŒUD PRINCIPAL : ESP32 FireBeetle
 *  Moniteur cardiaque connecté · Mini-projet ISIS 2026
 * =====================================================================
 *  - Lit le capteur DFRobot SEN0518 (MAX30102) : BPM + SpO2
 *      bus I2C n°1 (maître)  : SDA = GPIO25, SCL = GPIO26, adresse 0x57
 *  - Publie les mesures en JSON sur MQTT (broker Mosquitto du Raspberry Pi)
 *  - Répond au nœud de DIAGNOSTIC avec un code d'erreur (1 octet)
 *      bus I2C n°0 (esclave) : SDA = GPIO21, SCL = GPIO22, adresse 0x55
 *
 *  Bibliothèques : PubSubClient, DFRobot_BloodOxygen_S
 * =====================================================================
 */
#include <WiFi.h>
#include <PubSubClient.h>
#include "DFRobot_BloodOxygen_S.h"
#include "Wire.h"

// ================= RESEAU =================
const char* ssid        = "iot";
const char* password    = "iotisis;";
const char* mqtt_server = "172.18.32.39";
const char* MQTT_ID     = "ESP32_Principal_Cardio";

// Topics MQTT
#define T_DATA   "projet/principal/data"     // JSON complet
#define T_BPM    "projet/principal/bpm"
#define T_SPO2   "projet/principal/spo2"
#define T_CODE   "projet/principal/code"
#define T_ONLINE "projet/principal/online"   // online / offline (Last Will)
#define T_SEUIL  "projet/principal/seuil"    // reçu depuis Node-RED

// ================= I2C =================
#define SENSOR_SDA   25     // capteur (bus 1)
#define SENSOR_SCL   26
#define SLAVE_SDA    21     // liaison avec le nœud de diagnostic (bus 0)
#define SLAVE_SCL    22
#define I2C_ADDRESS  0x57   // adresse du capteur
#define I2C_DEV_ADDR 0x55   // adresse de cet ESP32 vu par le diagnostic

TwoWire I2C_2 = TwoWire(1);
DFRobot_BloodOxygen_S_I2C MAX30102(&I2C_2, I2C_ADDRESS);

WiFiClient espClient;
PubSubClient client(espClient);

// ================= CODES ERREUR (partagés avec le diagnostic) =================
#define ERR_NONE              0
#define ERR_WIFI_LOST         1
#define ERR_SENSOR_DISCONNECT 2
#define ERR_SENSOR_READ       3
#define ERR_TEMP_LIMIT        4
#define ERR_LOW_VOLTAGE       5
#define ERR_SERIAL_LOST       6
#define ERR_LOW_MEMORY        7
#define ERR_I2C               8
#define ERR_WATCHDOG          9
#define ERR_UNKNOWN           10

volatile uint8_t errorCode = ERR_SENSOR_DISCONNECT;
bool  capteurOK = false;
int   bpm = -1, spo2 = -1;
float temp = 0;
int   seuilBpm = 100;
unsigned long tCheck = 0, tMesure = 0, tWifi = 0, tMqtt = 0;

// ================= I2C ESCLAVE =================
// Appelé quand le diagnostic demande des données : on renvoie le code d'erreur
void onRequest() { Wire.write(errorCode); }
void onReceive(int len) { while (Wire.available()) Wire.read(); }

// ================= CAPTEUR =================
// Le capteur répond-il sur le bus ? (0 = accusé de réception reçu)
bool capteurPresent() {
  I2C_2.beginTransmission(I2C_ADDRESS);
  return I2C_2.endTransmission() == 0;
}

// ================= WIFI (non bloquant) =================
void gererWifi() {
  if (WiFi.status() != WL_CONNECTED && millis() - tWifi > 10000) {
    tWifi = millis();
    Serial.println("[WIFI] reconnexion...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
  }
}

// ================= MQTT =================
void callback(char* topic, byte* message, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)message[i];
  Serial.printf("[MQTT] %s : %s\n", topic, msg.c_str());

  if (strcmp(topic, T_SEUIL) == 0) {
    int s = msg.toInt();
    if (s >= 40 && s <= 220) seuilBpm = s;
  }
}

void gererMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (client.connected()) { client.loop(); return; }

  if (millis() - tMqtt > 5000) {
    tMqtt = millis();
    Serial.print("[MQTT] connexion... ");
    // Last Will : si l'ESP32 disparaît, le broker publie "offline" à sa place
    if (client.connect(MQTT_ID, T_ONLINE, 1, true, "offline")) {
      Serial.println("OK");
      client.publish(T_ONLINE, "online", true);
      client.subscribe(T_SEUIL);
    } else {
      Serial.printf("echec rc=%d\n", client.state());
    }
  }
}

void publierMqtt() {
  if (!client.connected()) return;

  bool doigt = (bpm > 0 && spo2 > 0);
  char json[200];
  snprintf(json, sizeof(json),
    "{\"bpm\":%d,\"spo2\":%d,\"temp\":%.1f,\"doigt\":%s,\"capteur\":%s,\"code\":%d,\"seuil\":%d}",
    bpm, spo2, temp, doigt ? "true" : "false", capteurOK ? "true" : "false",
    errorCode, seuilBpm);

  client.publish(T_DATA, json);
  client.publish(T_BPM,  String(bpm).c_str());
  client.publish(T_SPO2, String(spo2).c_str());
  client.publish(T_CODE, String(errorCode).c_str());
  Serial.printf("[MQTT] publie : %s\n", json);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(500);

  // 1) Esclave I2C en premier : le diagnostic reçoit un code même sans capteur
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
  Wire.begin((uint8_t)I2C_DEV_ADDR, SLAVE_SDA, SLAVE_SCL, 100000);
  Serial.println("ESP32 Slave initialise (0x55) sur GPIO21/22");

  // 2) Bus du capteur
  I2C_2.begin(SENSOR_SDA, SENSOR_SCL, 100000);

  // 3) WiFi + MQTT (sans bloquer)
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  tWifi = millis();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  client.setSocketTimeout(3);
}

// ================= LOOP =================
void loop() {
  gererWifi();
  gererMqtt();

  if (millis() - tCheck < 1000) return;
  tCheck = millis();

  // --- Capteur branché ?
  if (!capteurPresent()) {
    if (capteurOK) Serial.println("CAPTEUR PERDU (fil debranche ?)");
    capteurOK = false;
    bpm = -1; spo2 = -1;
    errorCode = ERR_SENSOR_DISCONNECT;
    publierMqtt();
    return;
  }

  // --- Capteur (re)branché : initialisation
  if (!capteurOK) {
    if (MAX30102.begin()) {
      MAX30102.sensorStartCollect();
      capteurOK = true;
      Serial.println("init success! start measuring...");
    } else {
      Serial.println("init fail!");
      errorCode = ERR_SENSOR_READ;
      return;
    }
  }

  // --- Mesure toutes les 4 s (le capteur met à jour ses données toutes les 4 s)
  if (millis() - tMesure < 4000) return;
  tMesure = millis();

  MAX30102.getHeartbeatSPO2();
  spo2 = MAX30102._sHeartbeatSPO2.SPO2;
  bpm  = MAX30102._sHeartbeatSPO2.Heartbeat;
  temp = MAX30102.getTemperature_C();
  Serial.printf("SPO2 = %d %% | BPM = %d | Temp = %.1f C\n", spo2, bpm, temp);

  // --- Code d'erreur (ordre = priorité)
  if (WiFi.status() != WL_CONNECTED) errorCode = ERR_WIFI_LOST;
  else if (temp < 0 || temp > 50)    errorCode = ERR_TEMP_LIMIT;
  else if (bpm <= 0)                 errorCode = ERR_SENSOR_READ;
  else                               errorCode = ERR_NONE;

  Serial.printf("Code erreur actuel : %d\n", errorCode);
  publierMqtt();
}
