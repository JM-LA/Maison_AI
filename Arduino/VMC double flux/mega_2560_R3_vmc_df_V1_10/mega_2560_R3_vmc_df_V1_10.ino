/*********************************************************************
 *  ARDUINO MEGA + W5500 + MQTT + Shield connection vis – VMC Double Flux (VMC-DF)
 *
 *  Module x6 relais HL-56S – ACTIF LOW (LOW = contact fermé)
 *
 *  Mapping production :
 *    IN1 -> POWER -> pin 24  (alimentation générale P1/P2)
 *    IN2 -> LF    -> pin 25  (bypass fermé = hiver, P1->X2)
 *    IN3 -> LO    -> pin 26  (bypass ouvert = été,  P1->X4)
 *    IN4 -> SH    -> pin 27  (complément pour vitesse moyenne)
 *    IN5 -> L2    -> pin 28  (petite vitesse, P2->X1)
 *    IN6 -> L1    -> pin 29  (grande vitesse, P2->X3)
 *
 *  Vitesses :
 *    - petite  : L2 seul          (L2 = ON,  L1 = OFF, SH = OFF)
 *    - moyenne : L2 + SH          (L2 = ON,  SH = ON,  L1 = OFF)
 *    - grande  : L1 seul          (L1 = ON,  L2 = OFF, SH = OFF)
 *
 *  Bypass :
 *    - hiver (fermé)  : LF ON,  LO OFF
 *    - été   (ouvert) : LO ON,  LF OFF
 *
 *  IMPORTANT :
 *    - LF & LO peuvent être tous les deux OFF UNIQUEMENT si POWER = OFF
 *    - Dès que POWER = ON : on est soit en "hiver" soit en "été", jamais les deux OFF.
 *
 *  MQTT :
 *    - Commandes :
 *        Mega_VMC_DF/cmd/power  -> "ON" / "OFF"
 *        Mega_VMC_DF/cmd/speed  -> "petite" / "moyenne" / "grande" (ou "1/2/3" compat)
 *        Mega_VMC_DF/cmd/bypass -> "ete" / "hiver"
 *
 *    - États :
 *        Mega_VMC_DF/state/power   -> "ON" / "OFF"
 *        Mega_VMC_DF/state/speed   -> "petite" / "moyenne" / "grande"
 *        Mega_VMC_DF/state/bypass  -> "ete" / "hiver"
 *
 * Il faut changer l'IP de votre Arduino ainsi que celui de votre Broker MQTT. 
 * Il vous faut aussi modifier l'ID et le Mot de Passe permettant de vous connecter à votre Broker MQTT
 *
 *********************************************************************/

#include <Ethernet.h>
#include <PubSubClient.h>

// ========================== CONFIG RÉSEAU ==========================
byte mac[]       = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x65};
IPAddress ip(192,168,1,XX);           // <--------------------------------------------------    IP de votre Mega VMC-DF   <-------------------------------------------------
IPAddress mqttServer(192,168,1,XX);   // <--------------------------------------------- IP de votre broker MQTT (Home Assistant / Mosquitto) <------------------------------

EthernetClient ethClient;
PubSubClient client(ethClient);

// ========================== RELAIS (ACTIF LOW) =====================
// Mapping physique définitif
#define RELAY_POWER  24   // IN1 - alimentation générale
#define RELAY_LF     25   // IN2 - LF (bypass fermé = hiver)
#define RELAY_LO     26   // IN3 - LO (bypass ouvert = été)
#define RELAY_SH     27   // IN4 - SH
#define RELAY_L2     28   // IN5 - L2
#define RELAY_L1     29   // IN6 - L1

const uint8_t RELAY_ON  = LOW;   // HL-56S : LOW = relais collé = courant passe
const uint8_t RELAY_OFF = HIGH;  // HIGH = relais relâché

// ========================== ÉTATS LOGIQUES =========================
bool   powerOn    = false;          // alimentation générale VMC
String speedMode  = "petite";       // "petite", "moyenne", "grande"
String bypassMode = "hiver";        // "ete" ou "hiver"

// ========================== OUTILS ================================
void relayOn(uint8_t pin)  { digitalWrite(pin, RELAY_ON);  }
void relayOff(uint8_t pin) { digitalWrite(pin, RELAY_OFF); }

void relaySafeWrite(uint8_t pin, bool on, uint16_t d = 50) {
  digitalWrite(pin, on ? RELAY_ON : RELAY_OFF);
  delay(d);
}

void publishState() {
  client.publish("Mega_VMC_DF/state/power",  powerOn ? "ON" : "OFF", true);
  client.publish("Mega_VMC_DF/state/speed",  speedMode.c_str(),      true);
  client.publish("Mega_VMC_DF/state/bypass", bypassMode.c_str(),     true);
}

// ========================== VITESSES ===============================
// Toujours couper les vitesses avant de reconfigurer, comme un sélecteur mécanique
void stopAllSpeeds() {
  relayOff(RELAY_L1);
  relayOff(RELAY_L2);
  relayOff(RELAY_SH);
  delay(80);
}

// petite : L2 seul (L2 ON, L1 OFF, SH OFF)
void setSpeedPetite() {
  // Si la VMC est coupée : on mémorise juste le mode sans toucher aux relais
  if (!powerOn) {
    speedMode = "petite";
    publishState();
    return;
  }

  stopAllSpeeds();
  relayOn(RELAY_L2);      // L2 ON
  // L1 OFF & SH OFF déjà garantis par stopAllSpeeds

  speedMode = "petite";
  publishState();
  Serial.println("🌀 Vitesse = PETITE (L2 seul)");
}

// moyenne : L2 + SH (L2 ON, SH ON, L1 OFF)
void setSpeedMoyenne() {
  if (!powerOn) {
    speedMode = "moyenne";
    publishState();
    return;
  }

  stopAllSpeeds();
  relayOn(RELAY_L2);      // L2 ON
  delay(80);
  relayOn(RELAY_SH);      // SH ON

  speedMode = "moyenne";
  publishState();
  Serial.println("🌀 Vitesse = MOYENNE (L2 + SH)");
}

// grande : L1 seul (L1 ON, L2 OFF, SH OFF)
void setSpeedGrande() {
  if (!powerOn) {
    speedMode = "grande";
    publishState();
    return;
  }

  stopAllSpeeds();
  relayOn(RELAY_L1);      // L1 ON

  speedMode = "grande";
  publishState();
  Serial.println("🌀 Vitesse = GRANDE (L1 seul)");
}

void setSpeedFromString(const String &mode) {
  if (mode == "petite")       setSpeedPetite();
  else if (mode == "moyenne") setSpeedMoyenne();
  else                        setSpeedGrande();  // défaut = grande si inconnu
}

// ========================== BYPASS (ÉTÉ / HIVER) ===================
// Rappel logique :
//  - Hiver : LF = ON, LO = OFF (bypass fermé, P1->X2)
//  - Été   : LO = ON, LF = OFF (bypass ouvert, P1->X4)
//  - Les deux OFF N'EXISTENT QUE SI POWER = OFF
void setBypassMode(const String &mode) {
  // On ne touche pas aux relais si la VMC est OFF.
  // On ne fait que mémoriser l'intention pour la prochaine mise sous tension.
  if (!powerOn) {
    if (mode == "ete") bypassMode = "ete";
    else               bypassMode = "hiver";
    publishState();
    Serial.print("🌬️ Bypass demandé (VMC OFF) -> ");
    Serial.println(bypassMode);
    return;
  }

  // VMC allumée : on applique physiquement
  if (mode == "ete") {
    // Été : LO ON, LF OFF
    relayOff(RELAY_LF);
    delay(80);
    relayOn(RELAY_LO);

    bypassMode = "ete";
    Serial.println("🌬️ Bypass = ÉTÉ (LO ON, LF OFF)");
  } else {
    // Hiver : LF ON, LO OFF
    relayOff(RELAY_LO);
    delay(80);
    relayOn(RELAY_LF);

    bypassMode = "hiver";
    Serial.println("🌬️ Bypass = HIVER (LF ON, LO OFF)");
  }

  publishState();
}

// ========================== POWER ================================
void setPower(bool on) {
  relaySafeWrite(RELAY_POWER, on);
  powerOn = on;

  Serial.print("⚡ Power -> ");
  Serial.println(powerOn ? "ON" : "OFF");

  if (powerOn) {
    // Quand on allume : on ré-applique la vitesse et le bypass mémorisés
    setSpeedFromString(speedMode);
    setBypassMode(bypassMode);  // ici powerOn=true donc ça agit sur les relais
  } else {
    // Quand on coupe :
    //  - on coupe toutes les vitesses
    //  - on coupe aussi le bypass (LF & LO OFF) → conforme à ta règle
    stopAllSpeeds();
    relayOff(RELAY_LF);
    relayOff(RELAY_LO);
  }

  publishState();
}

// ========================== MQTT CALLBACK ==========================
void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String msg = String((char*)payload);
  String t   = String(topic);

  msg.toLowerCase();

  Serial.print("MQTT ");
  Serial.print(t);
  Serial.print(" = ");
  Serial.println(msg);

  if (t.endsWith("power")) {
    setPower(msg == "on");
  }
  else if (t.endsWith("speed")) {
    // Compat éventuelle 1 / 2 / 3
    if (msg == "1")      setSpeedPetite();   // tu peux adapter cet ordre si tu veux
    else if (msg == "2") setSpeedMoyenne();
    else if (msg == "3") setSpeedGrande();
    else                 setSpeedFromString(msg);  // "petite/moyenne/grande"
  }
  else if (t.endsWith("bypass")) {
    setBypassMode(msg);   // "ete" / "hiver"
  }
}

// ========================== RECONNEXION MQTT =======================
void reconnect() {
  while (!client.connected()) {
    Serial.print("Connexion MQTT...");
    if (client.connect("Mega_VMC_DF", "ID", "MotDePasse")) {  // <------------------------------------------------- Modifier l'ID et le Mot de passe permettant de se connecter à votre Broker MQTT
      Serial.println("OK");
      client.subscribe("Mega_VMC_DF/cmd/#");
      client.publish("Mega_VMC_DF/status", "online");
      publishState();
    } else {
      Serial.print("Échec rc=");
      Serial.println(client.state());
      delay(5000);
    }
  }
}

// ========================== SETUP ================================
void setup() {
  Serial.begin(9600);
  Serial.println("=== DÉMARRAGE Mega_VMC_DF ===");

  Ethernet.begin(mac, ip);
  client.setServer(mqttServer, 1883);
  client.setCallback(callback);

  // Config pins relais
  int pins[] = {RELAY_POWER, RELAY_LF, RELAY_LO, RELAY_SH, RELAY_L2, RELAY_L1};
  for (int i = 0; i < 6; i++) {
    pinMode(pins[i], OUTPUT);
    digitalWrite(pins[i], RELAY_OFF);  // tout OFF au démarrage (y compris LF/LO)
  }

  // ÉTAT LOGIQUE INITIAL :
  //  - Power OFF
  //  - Vitesse "petite" mémorisée (appliquée quand on allume)
  //  - Bypass "hiver" mémorisé (appliqué quand on allume)
  powerOn    = false;
  speedMode  = "petite";
  bypassMode = "hiver";

  stopAllSpeeds();
  relayOff(RELAY_LF);
  relayOff(RELAY_LO);
  relayOff(RELAY_POWER);    // alim coupée

  publishState();
  reconnect();
}

// ========================== LOOP ================================
void loop() {
  if (!client.connected()) reconnect();
  client.loop();
}
