#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include "driver/i2s.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Electroniccats_PN7150.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <HTTPClient.h>
#include "esp_task_wdt.h"


/* ================= WIFI (HOTSPOT) ================= */
static const char* WIFI_SSID = "Boris's Galaxy S22";
static const char* WIFI_PASS = "123456789";

/* ================= OLED ================= */
#define OLED_SDA 21
#define OLED_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* ================= I2S MIC ================= */
#define SAMPLE_RATE 16000
#define I2S_SCK_IO  23
#define I2S_WS_IO   15
#define I2S_DI_IO   26
#define MODE_PIN    4
static const i2s_port_t I2S_PORT = I2S_NUM_0;

/* ================= SERVO (LEDC) ================= */
#define SERVO_PIN 14
#define SERVO_CH  0
#define SERVO_FREQ 50          // SG90 standard 50 Hz
#define SERVO_RES  16
#define SERVO_CLOSED_US  900
#define SERVO_OPEN_US    2000
bool windowOpen = false;

// VAŽNO: servo se NE attach-a u setupu (da ne trzne na boot)
static bool g_servoAttached = false;

/* ======= Timing ======= */
static const uint32_t SERVO_COOLDOWN_MS  = 1800;
static const uint32_t SERVO_OLED_HOLD_MS = 3500;

/* ======= NFC timing ======= */
static const uint32_t NFC_MSG_HOLD_MS    = 2500;
static const uint32_t NFC_PAUSE_AFTER_CARD_MS = 120000; // 2 minute mira

/* ================= NFC PN7150 ================= */
#define PN7150_IRQ  2
#define PN7150_VEN  5
#define PN7150_ADDR 0x28
Electroniccats_PN7150 nfc(PN7150_IRQ, PN7150_VEN, PN7150_ADDR, PN7150, &Wire);
RfIntf_t rf;

static bool g_nfcEnabled = true;
/* ===== I2C MUTEX ===== */
static SemaphoreHandle_t i2cMutex = nullptr;
static inline bool i2cTryLock(TickType_t toTicks) {
  return i2cMutex && xSemaphoreTake(i2cMutex, toTicks) == pdTRUE;
}
static inline void i2cUnlock() { if (i2cMutex) xSemaphoreGive(i2cMutex); }

/* =============== SESSION VARIJABLE ============= */
static const char* BASE_URL = "https://rus-sigurna-kutija-api-abcugrfhdnc2acc2.francecentral-01.azurewebsites.net/api";

String g_sessionId = "";
bool   g_sessionActive = false;

/* ================= FIXED DELAY ================= */
enum FlowState : uint8_t {
  FLOW_IDLE = 0,
  FLOW_WAIT_PHRASE
};

/* =============== WAV BUFFER ===================== */
static uint8_t* g_wavBuffer = nullptr;
static size_t   g_wavSize = 0;

static FlowState g_flow = FLOW_IDLE;
static uint32_t g_waitPhraseStartMs = 0;

// 8–10 sekundi, uzmimo 9000 ms
static const uint32_t WAIT_PHRASE_MS = 9000;

/* ================= AUDIO RECORDING ================= */
static int16_t* g_audioPcm16 = nullptr;
static size_t   g_audioSamples = 0;

static const uint32_t RECORD_MS = 4000;

/* ================= EMERGENCY UNLOCK ================= */
unsigned long lastEmergencyCheck = 0;
const unsigned long EMERGENCY_CHECK_INTERVAL = 3000; // 3 s


/* ===== OLED message queue ===== */
enum OledMsg : uint8_t {
  OLED_DOBRODOSLI = 1,
  OLED_WIFI_POVEZAN,
  OLED_PRISLONI_KARTICU,
  OLED_KARTICA_PRISLONJENA,
  OLED_GOVORI,
  OLED_KUTIJA_OTVORENA,
  OLED_KUTIJA_ZATVORENA,
  OLED_PRESTANI_GOVORITI,
  OLED_AUTENTIFIKACIJA_USPJESNA,
  OLED_KUTIJA_OTKLJUCANA,
  OLED_KUTIJA_ZAKLJUCANA,
  OLED_POKUSAJ_PONOVNO,
  OLED_KRIVA_FRAZA
};

struct OledEvent {
  OledMsg msg;
  uint16_t holdMs;
  uint8_t prio; // 0 = low (NFC), 1 = high (SERVO/MIC)
};

static QueueHandle_t oledQ = nullptr;

static void oledEnqueue(OledMsg msg, uint16_t holdMs, uint8_t prio) {
  if (!oledQ) return;
  OledEvent e{msg, holdMs, prio};
  if (prio) xQueueSendToFront(oledQ, &e, 0);
  else      xQueueSend(oledQ, &e, 0);
}

static bool oledDrawNow(OledMsg m) {
  if (!i2cTryLock(pdMS_TO_TICKS(120))) return false;

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 18);

  switch (m) {
    case OLED_DOBRODOSLI:          display.println("DOBRODOSLI"); break;
    case OLED_WIFI_POVEZAN:        display.println("WIFI"); display.println("POVEZAN"); break;
    case OLED_PRISLONI_KARTICU:    display.println("PRISLONI"); display.println("KARTICU"); break;
    case OLED_KARTICA_PRISLONJENA: display.println("KARTICA");  display.println("PRISLONJENA"); break;
    case OLED_GOVORI:              display.println("GOVORI"); break;
    case OLED_KUTIJA_OTVORENA:     display.println("KUTIJA");   display.println("OTVORENA"); break;
    case OLED_KUTIJA_ZATVORENA:    display.println("KUTIJA");   display.println("ZATVORENA"); break;
    case OLED_PRESTANI_GOVORITI:   display.println("PRESTANI"); display.println("GOVORITI"); break;
    case OLED_AUTENTIFIKACIJA_USPJESNA: display.println("AUTENTIFIKACIJA");display.println("USPJESNA"); break;
    case OLED_KUTIJA_OTKLJUCANA: display.println("KUTIJA"); display.println("OTKLJUCANA"); break;
    case OLED_POKUSAJ_PONOVNO:  display.println("POKUSAJ"); display.println("PONOVNO"); break;
    case OLED_KRIVA_FRAZA:      display.println("KRIVA");  display.println("FRAZA"); break;
    default: break;
  }

  display.display();
  i2cUnlock();
  return true;
}

/* ===== Servo helpers ===== */
static inline uint32_t usToDuty(uint32_t us) {
  const uint32_t period_us = 1000000UL / SERVO_FREQ;
  const uint32_t maxDuty = (1UL << SERVO_RES) - 1;
  return (uint32_t)((uint64_t)us * maxDuty / period_us);
}

static void servoAttachIfNeeded() {
  if (!g_servoAttached) {
    ledcAttachPin(SERVO_PIN, SERVO_CH);
    g_servoAttached = true;

    // start bez impulsa (duty 0) da se izbjegne trzaj pri attach-u
    ledcWrite(SERVO_CH, 0);
    delay(20);
  }
}

// ===== FIX: servo se pomakne pa se PWM ugasi (da se ne "vrti"/ne jittera) =====
static void servoWriteUS(uint32_t us) {
  if (us != SERVO_OPEN_US) return;

  servoAttachIfNeeded();

  // pošalji PWM da ode na poziciju "otključano"
  ledcWrite(SERVO_CH, usToDuty(us));

  // pusti ga kratko da stigne na poziciju
  delay(600);

  // ugasi PWM -> servo miruje
  ledcWrite(SERVO_CH, 0);
}

/* ===== I2S init ===== */
void initI2S() {
  pinMode(MODE_PIN, OUTPUT);
  digitalWrite(MODE_PIN, LOW);

  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_SCK_IO,
    .ws_io_num = I2S_WS_IO,
    .data_out_num = -1,
    .data_in_num = I2S_DI_IO
  };

  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_zero_dma_buffer(I2S_PORT);
}

bool recordAudioFixedMs(uint32_t durationMs) {

  const uint32_t samplesNeeded =
      (SAMPLE_RATE * durationMs) / 1000;

  // alokacija u PSRAM
  g_audioPcm16 = (int16_t*) ps_malloc(samplesNeeded * sizeof(int16_t));
  if (!g_audioPcm16) {
    Serial.println("PSRAM alloc FAILED");
    return false;
  }

  Serial.print("Recording samples: ");
  Serial.println(samplesNeeded);

  size_t samplesRecorded = 0;

  static int32_t i2sBuf[256 * 2]; // stereo frame buffer
  size_t bytesRead = 0;

  i2s_zero_dma_buffer(I2S_PORT);

  while (samplesRecorded < samplesNeeded) {

    if (i2s_read(
          I2S_PORT,
          i2sBuf,
          sizeof(i2sBuf),
          &bytesRead,
          pdMS_TO_TICKS(200)) != ESP_OK) {
      continue;
    }

    int frames = (bytesRead / 4) / 2; // 32-bit, stereo
    for (int i = 0; i < frames && samplesRecorded < samplesNeeded; i++) {

      int32_t left  = i2sBuf[i * 2];
      int32_t right = i2sBuf[i * 2 + 1];

      int32_t mono = (left + right) >> 1;

      g_audioPcm16[samplesRecorded++] = mono >> 12; // 32-bit → 16-bit
    }
  }

  g_audioSamples = samplesRecorded;

  Serial.print("Recording done, samples: ");
  Serial.println(g_audioSamples);

  return true;
}

bool buildWavBuffer() {

  const uint32_t dataSize = g_audioSamples * sizeof(int16_t);
  g_wavSize = 44 + dataSize;

  g_wavBuffer = (uint8_t*) ps_malloc(g_wavSize);
  if (!g_wavBuffer) {
    Serial.println("WAV alloc FAILED");
    return false;
  }

  uint8_t* p = g_wavBuffer;

  // RIFF
  memcpy(p, "RIFF", 4); p += 4;
  uint32_t chunkSize = g_wavSize - 8;
  memcpy(p, &chunkSize, 4); p += 4;
  memcpy(p, "WAVE", 4); p += 4;

  // fmt
  memcpy(p, "fmt ", 4); p += 4;
  uint32_t subChunk1Size = 16;
  memcpy(p, &subChunk1Size, 4); p += 4;
  uint16_t audioFormat = 1; // PCM
  memcpy(p, &audioFormat, 2); p += 2;
  uint16_t numChannels = 1;
  memcpy(p, &numChannels, 2); p += 2;
  uint32_t sampleRate = SAMPLE_RATE;
  memcpy(p, &sampleRate, 4); p += 4;
  uint32_t byteRate = SAMPLE_RATE * numChannels * 2;
  memcpy(p, &byteRate, 4); p += 4;
  uint16_t blockAlign = numChannels * 2;
  memcpy(p, &blockAlign, 2); p += 2;
  uint16_t bitsPerSample = 16;
  memcpy(p, &bitsPerSample, 2); p += 2;

  // data
  memcpy(p, "data", 4); p += 4;
  memcpy(p, &dataSize, 4); p += 4;

  // PCM samples
  memcpy(p, g_audioPcm16, dataSize);

  Serial.print("WAV size: ");
  Serial.println(g_wavSize);

  Serial.print("First 12 bytes: ");
  for (int i = 0; i < 12; i++) {
    Serial.print((char)g_wavBuffer[i]);
  }
  Serial.println();

  return true;
}

bool sendWavToSpeechToText(String& outText) {

  HTTPClient http;
  String url = String(BASE_URL) + "/speechToText";

  http.begin(url);
  http.addHeader("Content-Type", "application/octet-stream");

  int code = http.POST(g_wavBuffer, g_wavSize);

  Serial.print("STT HTTP code: ");
  Serial.println(code);

  if (code != 200) {
    http.end();
    return false;
  }

  String resp = http.getString();
  http.end();

  Serial.print("STT response: ");
  Serial.println(resp);

  int idx = resp.indexOf("\"text\"");
  if (idx < 0) return false;

  int start = resp.indexOf("\"", idx + 6) + 1;
  int end   = resp.indexOf("\"", start);
  outText = resp.substring(start, end);

  return true;
}

static String normalizeSpokenText(const String& s) {
  String out;
  out.reserve(s.length());

  bool prevSpace = true;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];

    // to lower
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';

    // keep only a-z and spaces
    if (c >= 'a' && c <= 'z') {
      out += c;
      prevSpace = false;
    } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!prevSpace) {
        out += ' ';
        prevSpace = true;
      }
    }
  }

  if (out.length() > 0 && out[out.length() - 1] == ' ') out.remove(out.length() - 1);
  return out;
}

static String urlEncode(const String& s) {
  const char* hex = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() * 3);

  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];

    bool safe =
      (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      c == '-' || c == '_' || c == '.' || c == '~';

    if (safe) {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[(c) & 0x0F];
    }
  }
  return out;
}

bool verifyTextWithCloud(const String& recognizedText, String& outStatus) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi nije spojen");
    return false;
  }

  String clean = normalizeSpokenText(recognizedText);

  String url = String(BASE_URL) + "/sessionVerifyText" +
               "?sessionId=" + urlEncode(g_sessionId) +
               "&spokenText=" + urlEncode(clean);

  Serial.print("VERIFY URL: ");
  Serial.println(url);

  HTTPClient http;
  http.begin(url);

  int code = http.GET();

  Serial.print("VERIFY HTTP code: ");
  Serial.println(code);

  String resp = http.getString();
  http.end();

  Serial.print("VERIFY response: ");
  Serial.println(resp);

  if (code != 200) return false;

  if (resp.indexOf("LOCKED") >= 0) outStatus = "LOCKED";
  else if (resp.indexOf("SUCCESS") >= 0 || resp.indexOf("\"success\":true") >= 0) outStatus = "SUCCESS";
  else outStatus = "FAIL";

  return true;
}

bool checkEmergencyUnlock() {
  HTTPClient http;
  http.begin("https://rus-sigurna-kutija-api-abcugrfhdnc2acc2.francecentral-01.azurewebsites.net/api/checkEmergencyUnlock");

  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    http.end();

    if (payload.indexOf("\"active\": true") >= 0) {
      return true;
    }
  }

  http.end();
  return false;
}

/* ===== WIFI connect ===== */
static void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Spajanje na WiFi: ");
  Serial.println(WIFI_SSID);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - t0 > 20000) {
      Serial.println("\nWiFi nije spojen (timeout).");
      t0 = millis();
    }
  }

  Serial.print("\nWiFi spojen! IP: ");
  Serial.println(WiFi.localIP());

  oledDrawNow(OLED_WIFI_POVEZAN);
  delay(800);

  oledDrawNow(OLED_DOBRODOSLI);
  delay(800);
}

/* ================= SESSION funkcija ================= */
bool startSessionWithCloud(const String& nfcUid) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi nije spojen");
    return false;
  }

  HTTPClient http;
  String url = String(BASE_URL) + "/sessionStart";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String body = "{\"nfcUid\":\"" + nfcUid + "\"}";

  int code = http.POST(body);

  Serial.print("HTTP status code: ");
  Serial.println(code);

  String resp = http.getString();

  Serial.print("Raw response: ");
  Serial.println(resp);

  http.end();

  if (code == 200) {
    int idx = resp.indexOf("\"sessionId\"");
    if (idx >= 0) {
      int start = resp.indexOf("\"", idx + 12) + 1;
      int end   = resp.indexOf("\"", start);
      g_sessionId = resp.substring(start, end);

      Serial.print("SESSION ID = ");
      Serial.println(g_sessionId);

      g_sessionActive = true;
      return true;
    }
  }

  return false;
}

/* ===== NFC init ===== */
static bool initNFC() {
  Wire.setClock(400000);

  pinMode(PN7150_IRQ, INPUT);
  pinMode(PN7150_VEN, OUTPUT);
  digitalWrite(PN7150_VEN, HIGH);

  if (!i2cTryLock(pdMS_TO_TICKS(250))) return false;

  bool ok = true;
  if (nfc.connectNCI()) ok = false;
  if (ok && nfc.ConfigureSettings()) ok = false;
  uint8_t mode = 1;
  if (ok && nfc.ConfigMode(mode)) ok = false;
  if (ok && nfc.StartDiscovery(mode) != 0) ok = false;

  i2cUnlock();
  return ok;
}

static void stopDiscovery() {
  if (i2cTryLock(pdMS_TO_TICKS(120))) { nfc.StopDiscovery(); i2cUnlock(); }
}

static void restartDiscovery() {
  stopDiscovery();
  vTaskDelay(pdMS_TO_TICKS(40));
  if (i2cTryLock(pdMS_TO_TICKS(120))) { nfc.StartDiscovery(1); i2cUnlock(); }
}

void setup() {
  esp_task_wdt_deinit();
  Serial.begin(115200);
  delay(200);

  i2cMutex = xSemaphoreCreateMutex();
  oledQ = xQueueCreate(12, sizeof(OledEvent));

  Wire.begin(OLED_SDA, OLED_SCL);
  if (i2cTryLock(pdMS_TO_TICKS(250))) {
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    i2cUnlock();
  }

  connectToWiFi();

  ledcSetup(SERVO_CH, SERVO_FREQ, SERVO_RES);
  windowOpen = false;

  initI2S();

  if (!initNFC()) {
    Serial.println("NFC INIT FAILED");
    while (1) delay(100);
  }

  oledDrawNow(OLED_PRISLONI_KARTICU);
}

void loop() {
  static uint32_t lastNfcCheck = 0;
  if (g_nfcEnabled && millis() - lastNfcCheck > 100) {
    lastNfcCheck = millis();

    if (digitalRead(PN7150_IRQ) == LOW) {
      if (i2cTryLock(pdMS_TO_TICKS(10))) {
        if (nfc.WaitForDiscoveryNotification(&rf) == 0) {
          String uid = "";
          for (uint8_t i = 0; i < rf.Info.NFC_APP.NfcIdLen; i++) {
            if (rf.Info.NFC_APP.NfcId[i] < 0x10) uid += "0";
            uid += String(rf.Info.NFC_APP.NfcId[i], HEX);
          }
          uid.toUpperCase();

          Serial.print("NFC UID: ");
          Serial.println(uid);

          if (!g_sessionActive) {
            Serial.println("Slanje UID-a u cloud (sessionStart)...");
            if (startSessionWithCloud(uid)) {
              Serial.println("Session started (cloud)");
              oledEnqueue(OLED_KARTICA_PRISLONJENA, 1500, 1);

              g_waitPhraseStartMs = millis();
              g_flow = FLOW_WAIT_PHRASE;

              g_nfcEnabled = false;   // pauza dok traje govor
            } else {
              Serial.println("Session start FAILED");
            }
          }

          stopDiscovery();
          restartDiscovery();
        }
        i2cUnlock();
      }
    }
  }

  switch (g_flow) {

    case FLOW_IDLE:
      break;

    case FLOW_WAIT_PHRASE:
      if (millis() - g_waitPhraseStartMs >= WAIT_PHRASE_MS) {
        Serial.println("Vrijeme za govor");
        oledDrawNow(OLED_GOVORI);

        Serial.println("Start recording...");
        recordAudioFixedMs(RECORD_MS);
        Serial.println("Recording finished");

        oledDrawNow(OLED_PRESTANI_GOVORITI);
        delay(700);

        if (!buildWavBuffer()) return;

        String recognizedText;
        if (sendWavToSpeechToText(recognizedText)) {

          Serial.print("Recognized text: ");
          Serial.println(recognizedText);

          String verifyStatus;
          if (verifyTextWithCloud(recognizedText, verifyStatus)) {

            Serial.print("VERIFY STATUS: ");
            Serial.println(verifyStatus);

            if (verifyStatus == "SUCCESS") {

              Serial.println("AUTENTIFIKACIJA USPJESNA");
              Serial.println("KUTIJA OTKLJUCANA");

              oledDrawNow(OLED_AUTENTIFIKACIJA_USPJESNA);
              delay(800);
              oledDrawNow(OLED_KUTIJA_OTKLJUCANA);

              Serial.println("OTVARANJE KUTIJE");
              servoWriteUS(SERVO_OPEN_US);
              windowOpen = true;

              g_flow = FLOW_IDLE;

              // ===== FIX: vrati NFC da opet čita kartice =====
              g_sessionActive = false;
              g_sessionId = "";
              g_nfcEnabled = true;
              oledDrawNow(OLED_PRISLONI_KARTICU);
            }
            else if (verifyStatus == "LOCKED") {
              Serial.println("KUTIJA ZAKLJUCANA (3/3)");
              oledDrawNow(OLED_KUTIJA_ZAKLJUCANA);

              g_flow = FLOW_IDLE;

              // ===== FIX: vrati NFC da opet čita kartice =====
              g_sessionActive = false;
              g_sessionId = "";
              g_nfcEnabled = true;
              oledDrawNow(OLED_PRISLONI_KARTICU);
            }
            else {
              Serial.println("POGRESNA FRAZA");

              oledDrawNow(OLED_KRIVA_FRAZA);
              delay(800);
              oledDrawNow(OLED_POKUSAJ_PONOVNO);
              g_waitPhraseStartMs = millis();
              g_flow = FLOW_WAIT_PHRASE;
            }
          } else {
            Serial.println("Verify request FAILED");
          }

        } else {
          Serial.println("STT failed");
        }

      }
      break;
  }

  static uint32_t holdUntil = 0;
  static uint8_t  currentPrio = 0;

  OledEvent e;
  if (xQueueReceive(oledQ, &e, 0) == pdTRUE) {
    uint32_t now = millis();

    if (now < holdUntil && e.prio < currentPrio) {
      // ignore low prio
    } else {
      uint32_t tStart = millis();
      while (!oledDrawNow(e.msg)) {
        delay(1);
        if (millis() - tStart > 250) break;
      }
      currentPrio = e.prio;
      holdUntil = millis() + e.holdMs;
    }
  }

  if (millis() - lastEmergencyCheck > EMERGENCY_CHECK_INTERVAL) {
    lastEmergencyCheck = millis();

    if (checkEmergencyUnlock()) {
      Serial.println("KUTIJA OTKLJUCANA U SLUCAJU NUZDE!");
      oledDrawNow(OLED_KUTIJA_OTKLJUCANA);
      servoWriteUS(SERVO_OPEN_US);
      windowOpen = true;

      g_flow = FLOW_IDLE;

      // ===== FIX: vrati NFC da opet čita kartice =====
      g_sessionActive = false;
      g_sessionId = "";
      g_nfcEnabled = true;
      oledDrawNow(OLED_PRISLONI_KARTICU);

      return;
    }
  }
}
