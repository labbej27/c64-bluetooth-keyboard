/*
 * C64C -> clavier Bluetooth (BLE HID) avec ESP32 DevKit V1
 * ---------------------------------------------------------
 * Scanne la matrice 8x8 du clavier C64 + RESTORE et envoie des rapports
 * HID clavier en BLE. Les codes sont choisis pour les keymaps VICE
 * positionnelles fournies dans vice/ (c64_ble_linux_fr.vkm, c64_ble_mac_fr.vkm).
 *
 * https://github.com/labbej27/c64-bluetooth-keyboard  -  licence MIT
 *
 * LED (celle du boîtier C64C, ou la LED bleue de la carte sur GPIO2) :
 *   clignote = en attente d'appairage / connexion
 *   fixe     = connecté
 *
 * Câblage : voir README.md
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>

#define DEVICE_NAME "C64 Keyboard"

// ---------------------------------------------------------------- Brochage
// PA0..PA7 (connecteur 13,19,18,17,16,15,14,20) -> sorties de balayage
static const uint8_t PA_PINS[8] = {13, 14, 27, 26, 25, 33, 32, 15};
// PB0..PB7 (connecteur 12,11,10,5,8,7,6,9) -> entrées avec pull-up
static const uint8_t PB_PINS[8] = {23, 22, 21, 19, 18, 17, 16, 4};
static const uint8_t RESTORE_PIN = 5;   // connecteur 3 (l'autre côté = pin 1 GND)
static const uint8_t LED_PIN = 2;       // LED C64C (+ LED bleue de la carte)

// ---------------------------------------------------------------- Codes HID
enum : uint8_t {
  K_A = 0x04, K_B, K_C, K_D, K_E, K_F, K_G, K_H, K_I, K_J, K_K, K_L, K_M,
  K_N, K_O, K_P, K_Q, K_R, K_S, K_T, K_U, K_V, K_W, K_X, K_Y, K_Z,
  K_1 = 0x1E, K_2, K_3, K_4, K_5, K_6, K_7, K_8, K_9, K_0,
  K_ENTER = 0x28, K_ESC, K_BKSP, K_TAB, K_SPACE, K_MINUS, K_EQUAL,
  K_LBRACKET, K_RBRACKET, K_BACKSLASH, K_NONUS_HASH, K_SEMICOLON, K_QUOTE,
  K_GRAVE, K_COMMA, K_DOT, K_SLASH,
  K_F1 = 0x3A, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8,
  K_HOME = 0x4A, K_PGUP, K_DEL, K_END, K_PGDN, K_RIGHT, K_LEFT, K_DOWN, K_UP,
};

// Pseudo-codes pour les modificateurs (>= 0xE0 = bit dans l'octet modifier)
#define M_LCTRL  0xE0
#define M_LSHIFT 0xE1
#define M_RSHIFT 0xE5

/*
 * Matrice [PA][PB]. Les codes sont des "positions physiques" US ;
 * le commentaire indique ce que ça donne sur le PC en AZERTY et la touche C64.
 * Lettres : choisies pour que le keysym AZERTY == la lettre C64
 * (A<->Q, Z<->W, M sur la touche ';' US).
 */
static const uint8_t KEYMAP[8][8] = {
  // PB0        PB1       PB2        PB3          PB4        PB5          PB6         PB7
  { K_BKSP,     K_ENTER,  K_RIGHT,   K_F7,        K_F1,      K_F3,        K_F5,       K_DOWN   }, // PA0: DEL RET CRSR→ F7 F1 F3 F5 CRSR↓
  { K_3,        K_Z,      K_Q,       K_4,         K_W,       K_S,         K_E,        M_LSHIFT }, // PA1: 3 W A 4 Z S E LSHIFT
  { K_5,        K_R,      K_D,       K_6,         K_C,       K_F,         K_T,        K_X      }, // PA2: 5 R D 6 C F T X
  { K_7,        K_Y,      K_G,       K_8,         K_B,       K_H,         K_U,        K_V      }, // PA3: 7 Y G 8 B H U V
  { K_9,        K_I,      K_J,       K_0,         K_SEMICOLON, K_K,       K_O,        K_N      }, // PA4: 9 I J 0 M K O N
  { K_MINUS,    K_P,      K_L,       K_EQUAL,     K_COMMA,   K_DOT,       K_LBRACKET, K_M      }, // PA5: + P L - . : @ ,
  { K_END,      K_RBRACKET, K_QUOTE,  K_HOME,     M_RSHIFT,  K_SLASH,     K_BACKSLASH, K_PGDN  }, // PA6: £ * ; HOME RSHIFT = ↑ /
  { K_1,        K_GRAVE,  K_TAB,     K_2,         K_SPACE,   M_LCTRL,     K_A,        K_ESC    }, // PA7: 1 ← CTRL 2 SPACE C= Q RUN/STOP
};
static const uint8_t RESTORE_CODE = K_PGUP;

// ---------------------------------------------------------------- HID BLE
static const uint8_t REPORT_MAP[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,       // Generic Desktop / Keyboard / Application
  0x85, 0x01,                               //   Report ID 1
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,       //   Modifiers E0..E7
  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,       //   Reserved byte
  0x95, 0x05, 0x75, 0x01, 0x05, 0x08,       //   LEDs (output)
  0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00,       //   6 keycodes
  0x25, 0x73, 0x05, 0x07, 0x19, 0x00, 0x29, 0x73, 0x81, 0x00,
  0xC0
};

static BLEHIDDevice *hid;
static BLECharacteristic *inputKbd;
static volatile bool connected = false;
static volatile uint32_t connectedAt = 0;

// Intervalle de connexion préféré (unités de 1,25 ms) : 11,25–30 ms.
// Valeurs conformes aux règles Apple pour un périphérique HID
// (min >= 11,25 ms et max >= min + 15 ms), acceptées aussi par Windows/Linux.
#define CI_MIN 9
#define CI_MAX 24
static BLEServer *server;
#if defined(CONFIG_BLUEDROID_ENABLED)
static esp_bd_addr_t peerAddr;
#else
static uint16_t peerHandle;
#endif
static bool paramsRequested = false;

class ServerCB : public BLEServerCallbacks {
#if defined(CONFIG_BLUEDROID_ENABLED)
  void onConnect(BLEServer *, esp_ble_gatts_cb_param_t *p) override {
    memcpy(peerAddr, p->connect.remote_bda, sizeof(esp_bd_addr_t));
#else
  void onConnect(BLEServer *, ble_gap_conn_desc *d) override {
    peerHandle = d->conn_handle;
#endif
    connected = true;
    connectedAt = millis();
    paramsRequested = false;
    Serial.println("BLE connecte");
  }
  void onDisconnect(BLEServer *) override {
    connected = false;
    Serial.println("BLE deconnecte");
    BLEDevice::startAdvertising();
  }
};

// Détecte les envois refusés par la pile BLE pour les renvoyer.
static volatile bool sendFailed = false;
static uint32_t failCount = 0;
class ReportCB : public BLECharacteristicCallbacks {
  void onStatus(BLECharacteristic *, Status s, uint32_t code) override {
    if (s != SUCCESS_NOTIFY) {
      sendFailed = true;
      failCount++;
      Serial.printf("envoi refuse (status %d, code %u)\n", (int)s, (unsigned)code);
    }
  }
};

class SecurityCB : public BLESecurityCallbacks {
  bool onSecurityRequest() override { return true; }
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t) override {}
  bool onConfirmPIN(uint32_t) override { return true; }
#if defined(CONFIG_BLUEDROID_ENABLED)
  void onAuthenticationComplete(esp_ble_auth_cmpl_t r) override {
    Serial.printf("Appairage %s\n", r.success ? "OK" : "ECHEC");
  }
#elif defined(CONFIG_NIMBLE_ENABLED)
  void onAuthenticationComplete(ble_gap_conn_desc *d) override {
    Serial.printf("Appairage %s\n", d->sec_state.encrypted ? "OK" : "ECHEC");
  }
#endif
};

// ---------------------------------------------------------------- Scan
// Anti-rebond "immédiat" : un changement est pris en compte tout de suite,
// puis la touche est ignorée pendant LOCKOUT_SCANS balayages (rebonds).
static const uint8_t LOCKOUT_SCANS = 8;     // ~8 ms
static bool keyState[8][8];
static uint8_t keyCount[8][8];
static bool restoreState = false;
static uint8_t restoreCount = 0;

static bool debounce(bool raw, bool &state, uint8_t &lock) {
  if (lock) { lock--; return false; }
  if (raw == state) return false;
  state = raw;
  lock = LOCKOUT_SCANS;
  return true;
}

static bool scanMatrix() {
  bool changed = false;
  for (uint8_t a = 0; a < 8; a++) {
    pinMode(PA_PINS[a], OUTPUT);
    digitalWrite(PA_PINS[a], LOW);
    delayMicroseconds(30);                  // stabilisation (câble + pull-ups)
    for (uint8_t b = 0; b < 8; b++) {
      bool raw = digitalRead(PB_PINS[b]) == LOW;
      if (debounce(raw, keyState[a][b], keyCount[a][b])) {
        changed = true;
        Serial.printf("PA%u PB%u %s\n", a, b, keyState[a][b] ? "appui" : "relache");
      }
    }
    pinMode(PA_PINS[a], INPUT);             // haute impédance hors balayage
  }
  if (debounce(digitalRead(RESTORE_PIN) == LOW, restoreState, restoreCount)) {
    changed = true;
    Serial.printf("RESTORE %s\n", restoreState ? "appui" : "relache");
  }
  return changed;
}

static void buildReport(uint8_t rep[8]) {
  memset(rep, 0, 8);
  uint8_t n = 2;
  auto add = [&](uint8_t code) {
    if (code >= 0xE0) rep[0] |= 1 << (code - 0xE0);
    else if (code && n < 8) rep[n++] = code;
  };
  for (uint8_t a = 0; a < 8; a++)
    for (uint8_t b = 0; b < 8; b++)
      if (keyState[a][b]) add(KEYMAP[a][b]);
  if (restoreState) add(RESTORE_CODE);
}

// File des rapports à envoyer
static const uint8_t QSIZE = 32;
static const uint32_t MIN_GAP_MS = 15;   // >= intervalle BLE typique d'un Mac (15 ms)
static uint8_t queue[QSIZE][8];
static uint8_t qHead = 0, qCount = 0;
static uint8_t lastQueued[8] = {0};

static void sendReport(const uint8_t rep[8]) {
  inputKbd->setValue((uint8_t *)rep, 8);
  inputKbd->notify();
}

// ---------------------------------------------------------------- Setup/loop
void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < 8; i++) {
    pinMode(PA_PINS[i], INPUT);
    pinMode(PB_PINS[i], INPUT_PULLUP);
  }
  pinMode(RESTORE_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  BLEDevice::init(DEVICE_NAME);
  BLESecurity *sec = new BLESecurity();
  sec->setCapability(ESP_IO_CAP_NONE);
  sec->setAuthenticationMode(true, false, true);   // bonding, pas de PIN, SC
  BLEDevice::setSecurityCallbacks(new SecurityCB());

  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCB());

  hid = new BLEHIDDevice(server);
  hid->manufacturer()->setValue("Commodore (DIY)");
  hid->pnp(0x02, 0xE502, 0xC064, 0x0100);
  hid->hidInfo(0x00, 0x01);
  hid->reportMap((uint8_t *)REPORT_MAP, sizeof(REPORT_MAP));
  inputKbd = hid->inputReport(1);
  inputKbd->setCallbacks(new ReportCB());
  hid->outputReport(1);                            // LEDs (ignorées)
  hid->setBatteryLevel(100);
  hid->startServices();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->setAppearance(0x03C1);                      // HID clavier
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->setScanResponse(true);
  adv->setMinPreferred(CI_MIN);
  adv->setMaxPreferred(CI_MAX);
  BLEDevice::startAdvertising();

  Serial.println("C64 BLE keyboard pret, en attente de connexion...");
}

void loop() {
  static uint32_t lastScan = 0;
  static bool wasConnected = false;
  uint32_t now = millis();

  // LED : clignote si pas connecté, fixe si connecté
  digitalWrite(LED_PIN, connected ? HIGH : ((now / 500) & 1));

  if (now - lastScan < 1) return;                  // ~1 balayage / ms
  lastScan = now;
  scanMatrix();

  // Chaque état différent du clavier est mis en file : même une frappe plus
  // courte que l'intervalle BLE donne un rapport "appui" PUIS un "relâché".
  uint8_t rep[8];
  buildReport(rep);
  const uint8_t *tail = qCount ? queue[(qHead + qCount - 1) % QSIZE] : lastQueued;
  if (memcmp(rep, tail, 8) != 0) {
    if (qCount == QSIZE) { qHead = (qHead + 1) % QSIZE; qCount--; }  // file pleine : on jette le plus ancien
    memcpy(queue[(qHead + qCount) % QSIZE], rep, 8);
    qCount++;
  }

  if (connected != wasConnected) {
    wasConnected = connected;
    qCount = 0;
    memcpy(queue[0], rep, 8); qHead = 0; qCount = 1;  // après (re)connexion : renvoie l'état courant
  }
  if (!connected || now - connectedAt < 1500) return;  // laisse l'hôte s'abonner

  // Demande un intervalle de connexion court, une fois la liaison établie
  // (l'hôte ignore souvent les préférences annoncées : Linux prenait 45 ms).
  if (!paramsRequested && now - connectedAt > 3000) {
    paramsRequested = true;
#if defined(CONFIG_BLUEDROID_ENABLED)
    server->updateConnParams(peerAddr, CI_MIN, CI_MAX, 0, 400);
#else
    server->updateConnParams(peerHandle, CI_MIN, CI_MAX, 0, 400);
#endif
  }

  // Envoi : un rapport au plus toutes les MIN_GAP_MS ; si la pile refuse,
  // le même rapport est renvoyé au tour suivant (rien n'est perdu).
  static uint32_t lastSend = 0;
  if (qCount && now - lastSend >= MIN_GAP_MS) {
    lastSend = now;
    sendFailed = false;
    sendReport(queue[qHead]);                      // onStatus() est appelé pendant l'envoi
    if (!sendFailed) {
      memcpy(lastQueued, queue[qHead], 8);
      qHead = (qHead + 1) % QSIZE;
      qCount--;
    }
  }
}
