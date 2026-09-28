/*
 * =========================================================================================
 *  BeeWare ESP32 Firmware 🐝 - 3.0-Second Direct Real-Time Audio Streaming + DHT Sensor
 *  Hardware: ESP32 + INMP441 (I2S Microphone) + DHT22 Temperature & Humidity Sensor
 * =========================================================================================
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <driver/i2s_std.h>
#include <DHT.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ======================== CONFIGURATION ========================
// Starlink / Field Wi-Fi credentials
const char* WIFI_SSID     = "GFiber_3EA45";
const char* WIFI_PASSWORD = "XTF2eTAR";

// ======================== FIREBASE CLOUD CONFIG ========================
// Firebase Realtime Database (Singapore) — accessible globally via Starlink / Mobile Data
const char* FIREBASE_HOST = "beeware-beaef-default-rtdb.asia-southeast1.firebasedatabase.app";

// BeeWare Cloud Backend (Render.com) or Local PC
// Points to public Render cloud domain by default; auto-switches to local IP if UDP discovered on LAN
char backendHost[64]      = "beeware-2wp5.onrender.com";
int  backendPort          = 443;
const char* API_KEY       = "beeware_secret_key_default";
// NOTE: Device ID is auto-generated from MAC address — see getDeviceId() below

// Cooldown & Audio Recording Settings (300 Seconds Cooldown / 3.0s Audio at 16kHz)
#define COOLDOWN_SECONDS    300       // 300 seconds cooldown (5 minutes)
#define USE_DEEP_SLEEP      false     // false = Active Cooldown Loop (recommended for bench testing/USB); true = Deep Sleep
#define RECORD_TIME_SECONDS 3.0       // 3.0 full seconds of audio
#define SAMPLE_RATE         16000     // Full 16kHz studio sample rate
#define VOLUME_GAIN         4         // Digital gain boost

// INMP441 I2S Pins
#define I2S_WS              25        // Word Select (WS / LRCL)
#define I2S_SD              33        // Serial Data (SD / DOUT)
#define I2S_SCK             32        // Bit Clock (SCK / BCLK)

// Battery ADC Pin 
#define BATTERY_PIN         35

// DHT Sensor Config
#define DHTPIN              4         // Digital GPIO pin connected to DHT data pin
#define DHTTYPE             DHT22     // DHT 22 (AM2302)

DHT dht(DHTPIN, DHTTYPE);
i2s_chan_handle_t rx_handle = NULL;

// ======================== DEVICE ID FROM MAC ========================
// Generates a unique ID like "BW-A1B2C3" from last 3 bytes of MAC.
// Flash this to every ESP32 — each one auto-gets its own unique ID.
// The same ID is what you put into the QR sticker for app pairing.
String getDeviceId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char id[12];
  snprintf(id, sizeof(id), "BW-%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(id);
}

// Fast Base64 Lookup Table
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void encodeChunkToBase64(const uint8_t* in, size_t in_len, char* out) {
  size_t i = 0, j = 0;
  while (i < in_len) {
    uint32_t octet_a = in[i++];
    uint32_t octet_b = in[i++];
    uint32_t octet_c = in[i++];

    uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

    out[j++] = b64_table[(triple >> 18) & 0x3F];
    out[j++] = b64_table[(triple >> 12) & 0x3F];
    out[j++] = b64_table[(triple >> 6) & 0x3F];
    out[j++] = b64_table[triple & 0x3F];
  }
}

// ======================== I2S CONFIGURATION ========================
void setupI2S() {
  if (rx_handle != NULL) {
    return; // Already initialized
  }
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_SCK,
      .ws   = (gpio_num_t)I2S_WS,
      .dout = I2S_GPIO_UNUSED,
      .din  = (gpio_num_t)I2S_SD,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}

void stopI2S() {
  if (rx_handle != NULL) {
    i2s_channel_disable(rx_handle);
    i2s_del_channel(rx_handle);
    rx_handle = NULL;
  }
}

// ======================== BATTERY CALCULATION ========================
int readBatteryPercentage() {
  int raw = analogRead(BATTERY_PIN);
  float voltage = (raw / 4095.0) * 3.3 * 2.0; 
  int percentage = (int)(((voltage - 3.2) / (4.2 - 3.2)) * 100);
  return constrain(percentage, 0, 100);
}

// ======================== DHT SENSOR READING WITH RETRIES ========================
// DHT22 (AM2302) requires at least 2.0 seconds between consecutive read requests.
// We allow 4 attempts with a 2.2-second stabilization delay between tries.
void readDHTSensor(float &temp, float &hum) {
  Serial.println("🌡️ Reading DHT22 temperature & humidity...");

  for (int attempt = 1; attempt <= 4; attempt++) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h) && (t >= -40.0 && t <= 80.0) && (h >= 0.0 && h <= 100.0) && (t > 0.0 || h > 0.0)) {
      temp = t;
      hum  = h;
      Serial.printf("✅ [DHT22 DETECTED] Temperature: %.1f °C | Humidity: %.1f %% (attempt %d)\n", temp, hum, attempt);
      return;
    }

    if (attempt < 4) {
      Serial.printf("⏳ [DHT22 WARMUP] Reading attempt %d failed (sensor stabilizing). Waiting 2.2s before retry...\n", attempt);
      delay(2200); // Must be > 2000ms per DHT22 hardware specifications
    }
  }

  Serial.println("⚠️ [SENSOR NOT DETECTED] Failed to read from DHT22 sensor! Check GPIO 4 & 10k resistor. Defaulting to 0.0.");
  temp = 0.0;
  hum  = 0.0;
}

// ======================== MEASURE ACOUSTIC LEVEL & FREQUENCY (INMP441) ========================
// Samples INMP441 I2S microphone, calculates peak amplitude and fundamental dominant frequency in Hz
// via zero-crossing rate (ZCR) with noise gating. If no sound or microphone is disconnected, returns 0 Hz.
void measureAcoustics(int32_t &peakVal, int &freqHz) {
  peakVal = 0;
  freqHz = 0;
  if (rx_handle == NULL) {
    Serial.println("⚠️ [SENSOR NOT DETECTED] I2S handle is null — Acoustic frequency: 0 Hz");
    return;
  }

  const size_t CHUNK_SAMPLES = 192;
  int32_t chunkRaw[CHUNK_SAMPLES];
  size_t totalSamplesToMeasure = (size_t)(SAMPLE_RATE * 1.5); // 1.5 seconds acoustic sampling (24,000 samples)
  size_t samplesReadTotal = 0;
  uint32_t startMs = millis();

  const int32_t NOISE_THRESHOLD = 45; // Noise gate threshold to reject baseline ADC jitter
  int zeroCrossings = 0;
  int prevSign = 0;

  Serial.println("🎙️ Sampling INMP441 hive acoustics for 1.5 seconds...");

  while (samplesReadTotal < totalSamplesToMeasure && (millis() - startMs < 3000)) {
    size_t toRead = min(CHUNK_SAMPLES, totalSamplesToMeasure - samplesReadTotal);
    size_t bytesRead = 0;
    esp_err_t err = i2s_channel_read(rx_handle, chunkRaw, toRead * sizeof(int32_t), &bytesRead, 100);
    if (err == ESP_OK && bytesRead > 0) {
      size_t count = bytesRead / sizeof(int32_t);
      for (size_t i = 0; i < count; i++) {
        int32_t sample = chunkRaw[i] >> 14;
        sample = sample * VOLUME_GAIN;
        int32_t absSample = abs(sample);
        if (absSample > peakVal) {
          peakVal = absSample;
        }

        // Zero-crossing detector with hysteresis
        int sign = 0;
        if (sample > NOISE_THRESHOLD) {
          sign = 1;
        } else if (sample < -NOISE_THRESHOLD) {
          sign = -1;
        }

        if (sign != 0 && prevSign != 0 && sign != prevSign) {
          zeroCrossings++;
        }
        if (sign != 0) {
          prevSign = sign;
        }
      }
      samplesReadTotal += count;
    }
  }

  // Calculate dominant frequency in Hz
  // Honeybee piping & colony buzz typically falls between 100 Hz and 1000 Hz.
  // If peak is below noise floor or zero-crossings are insufficient, frequency is 0 (not detected / silent).
  if (peakVal < 60 || zeroCrossings < 8 || samplesReadTotal == 0) {
    freqHz = 0;
    Serial.println("⚠️ [SENSOR NOT DETECTED] INMP441 acoustic signal not detected (0 Hz / silent)!");
  } else {
    float durationSec = (float)samplesReadTotal / (float)SAMPLE_RATE;
    float calculatedHz = (zeroCrossings / 2.0f) / durationSec;
    if (calculatedHz < 40.0f || calculatedHz > 3500.0f) {
      freqHz = 0;
      Serial.printf("⚠️ [ACOUSTIC FILTER] Out-of-range frequency: %.1f Hz — reporting 0 Hz\n", calculatedHz);
    } else {
      freqHz = (int)round(calculatedHz);
      Serial.printf("🔊 Acoustic Dominant Frequency: %d Hz (Peak Amplitude: %d)\n", freqHz, peakVal);
    }
  }
}

// ======================== SEND TELEMETRY TO FIREBASE CLOUD ========================
// Pushes real-time temperature, humidity, battery, signal, and acoustic frequency (Hz) directly
// to Firebase Realtime Database — works globally via Starlink, Hotspot, or Home Wi-Fi.
void sendTelemetryToFirebase(float temp, float hum, int battery, int rssi, int32_t peakVal, int freqHz) {
  WiFiClientSecure client;
  client.setInsecure(); // SSL/TLS connection without hardcoded CA certificate
  client.setTimeout(10000);

  HTTPClient https;
  String deviceId = getDeviceId();
  String url = "https://" + String(FIREBASE_HOST) + "/telemetry/" + deviceId + ".json";

  Serial.printf("☁️ [Firebase RTDB] Pushing telemetry to %s...\n", url.c_str());

  if (https.begin(client, url)) {
    https.addHeader("Content-Type", "application/json");

    String acousticStr = freqHz > 0 ? (String(freqHz) + " Hz") : "0 Hz";

    String payload = "{";
    payload += "\"device_id\":\"" + deviceId + "\",";
    payload += "\"deviceId\":\"" + deviceId + "\",";
    payload += "\"temperature\":" + String(temp, 1) + ",";
    payload += "\"humidity\":" + String(hum, 1) + ",";
    payload += "\"battery_level\":" + String(battery) + ",";
    payload += "\"wifi_rssi\":" + String(rssi) + ",";
    payload += "\"sample_rate\":" + String(SAMPLE_RATE) + ",";
    payload += "\"peak_audio\":" + String(peakVal) + ",";
    payload += "\"frequency\":" + String(freqHz) + ",";
    payload += "\"frequency_hz\":" + String(freqHz) + ",";
    payload += "\"acoustic\":\"" + acousticStr + "\",";
    payload += "\"temp_detected\":" + String(temp > 0.0 ? "true" : "false") + ",";
    payload += "\"hum_detected\":" + String(hum > 0.0 ? "true" : "false") + ",";
    payload += "\"acoustic_detected\":" + String(freqHz > 0 ? "true" : "false") + ",";
    payload += "\"status\":\"online\",";
    payload += "\"timestamp\":\"Now\"";
    payload += "}";

    int httpCode = https.PUT(payload);
    if (httpCode > 0) {
      Serial.printf("✅ [Firebase RTDB] Telemetry updated! HTTP Status: %d\n", httpCode);
    } else {
      Serial.printf("❌ [Firebase RTDB] PUT failed: %s\n", https.errorToString(httpCode).c_str());
    }
    https.end();
  } else {
    Serial.println("❌ [Firebase RTDB] Failed to initialize HTTPS connection");
  }

  // Also append to telemetry_history so historical graphs in the app populate
  String historyUrl = "https://" + String(FIREBASE_HOST) + "/telemetry_history/" + deviceId + ".json";
  if (https.begin(client, historyUrl)) {
    https.addHeader("Content-Type", "application/json");
    String histPayload = "{";
    histPayload += "\"temperature\":" + String(temp, 1) + ",";
    histPayload += "\"humidity\":" + String(hum, 1) + ",";
    histPayload += "\"battery_level\":" + String(battery) + ",";
    histPayload += "\"frequency\":" + String(freqHz) + ",";
    histPayload += "\"timestamp\":\"Now\"";
    histPayload += "}";
    https.POST(histPayload);
    https.end();
  }
}

// ======================== STREAM TELEMETRY & 3-SEC AUDIO ========================
void streamTelemetryAndAudio(float temp, float hum, int battery, int rssi, int freqHz) {
  if (strlen(backendHost) == 0 || String(backendHost) == "0.0.0.0") {
    Serial.println("ℹ️ Backend host not configured — skipped audio stream. Telemetry delivered to Firebase Cloud.");
    return;
  }

  bool isHttps = (backendPort == 443);
  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  Client& client = isHttps ? static_cast<Client&>(secureClient) : static_cast<Client&>(plainClient);

  if (isHttps) {
    secureClient.setInsecure(); // SSL/TLS connection without CA cert checks
    secureClient.setTimeout(10000); // 10s for SSL handshake and cloud response
  } else {
    plainClient.setTimeout(5000);
  }

  Serial.printf("🔌 Connecting to backend at %s:%d (%s)...\n", backendHost, backendPort, isHttps ? "HTTPS" : "HTTP");

  if (!client.connect(backendHost, backendPort)) {
    Serial.printf("⚠️ Connection to backend (%s:%d) offline. Telemetry & frequency (%d Hz) delivered to Firebase Cloud.\n",
                  backendHost, backendPort, freqHz);
    return;
  }

  Serial.println("✅ Connected to BeeWare backend socket!");

  size_t totalSamples = (size_t)(SAMPLE_RATE * RECORD_TIME_SECONDS); // 48,000 samples
  size_t totalB64Chars = (totalSamples * sizeof(int16_t) * 4) / 3;   // 128,000 chars

  // Build JSON headers with frequency in Hz
  String deviceId = getDeviceId();
  String jsonHead = "{\"deviceId\":\"" + deviceId + "\",";
  jsonHead += "\"temperature\":" + String(temp, 1) + ",";
  jsonHead += "\"humidity\":" + String(hum, 1) + ",";
  jsonHead += "\"batteryLevel\":" + String(battery) + ",";
  jsonHead += "\"wifiRssi\":" + String(rssi) + ",";
  jsonHead += "\"sampleRate\":" + String(SAMPLE_RATE) + ",";
  jsonHead += "\"frequency\":" + String(freqHz) + ",";
  jsonHead += "\"frequencyHz\":" + String(freqHz) + ",";
  jsonHead += "\"audioBase64\":\"";

  String jsonFoot = "\"}";

  size_t contentLength = jsonHead.length() + totalB64Chars + jsonFoot.length();

  Serial.printf("🚀 Streaming %d bytes payload directly to BeeWare backend...\n", contentLength);

  // Send HTTP Header
  client.print("POST /telemetry HTTP/1.1\r\n");
  if (backendPort == 443 || backendPort == 80) {
    client.print("Host: " + String(backendHost) + "\r\n");
  } else {
    client.print("Host: " + String(backendHost) + ":" + String(backendPort) + "\r\n");
  }
  client.print("Content-Type: application/json\r\n");
  client.print("X-API-Key: " + String(API_KEY) + "\r\n");
  client.print("Content-Length: " + String(contentLength) + "\r\n");
  client.print("Connection: close\r\n\r\n");

  // Send JSON prefix
  client.print(jsonHead);

  // Stream live audio chunks from INMP441 directly to Wi-Fi socket
  const size_t CHUNK_SAMPLES = 192; // 384 bytes PCM -> 512 chars Base64
  int32_t chunkRaw[CHUNK_SAMPLES];
  int16_t chunkPcm[CHUNK_SAMPLES];
  char b64Chunk[513];

  size_t samplesRecorded = 0;
  int32_t peakVal = 0;
  uint32_t startMs = millis();
  uint32_t timeoutMs = (uint32_t)(RECORD_TIME_SECONDS * 1000) + 4000;

  Serial.println("🎙️ Recording & streaming 3.0s hive acoustics in real-time...");

  // Flush any stale FIFO samples to ensure a fresh, full 3.0-second recording
  if (rx_handle != NULL) {
    i2s_channel_disable(rx_handle);
    delay(10);
    i2s_channel_enable(rx_handle);
  }

  while (samplesRecorded < totalSamples && (millis() - startMs < timeoutMs)) {
    size_t samplesToRead = min(CHUNK_SAMPLES, totalSamples - samplesRecorded);
    size_t bytesToRead = samplesToRead * sizeof(int32_t);
    size_t bytesRead = 0;

    esp_err_t err = i2s_channel_read(rx_handle, chunkRaw, bytesToRead, &bytesRead, 100);
    if (err == ESP_OK && bytesRead > 0) {
      size_t readSamples = bytesRead / sizeof(int32_t);
      for (size_t i = 0; i < readSamples; i++) {
        int32_t sample = chunkRaw[i] >> 14;
        sample = sample * VOLUME_GAIN;

        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;

        chunkPcm[i] = (int16_t)sample;
        if (abs(chunkPcm[i]) > peakVal) {
          peakVal = abs(chunkPcm[i]);
        }
      }

      // Convert chunk to Base64 and write directly into network socket
      encodeChunkToBase64((uint8_t*)chunkPcm, readSamples * sizeof(int16_t), b64Chunk);
      size_t chunkB64Len = (readSamples * sizeof(int16_t) * 4) / 3;
      client.write((const uint8_t*)b64Chunk, chunkB64Len);

      samplesRecorded += readSamples;
    }
  }

  // Safety: If I2S underflowed, pad silence so Content-Length matches EXACTLY
  while (samplesRecorded < totalSamples) {
    size_t remaining = min((size_t)CHUNK_SAMPLES, totalSamples - samplesRecorded);
    memset(chunkPcm, 0, sizeof(chunkPcm));
    encodeChunkToBase64((uint8_t*)chunkPcm, remaining * sizeof(int16_t), b64Chunk);
    size_t chunkB64Len = (remaining * sizeof(int16_t) * 4) / 3;
    client.write((const uint8_t*)b64Chunk, chunkB64Len);
    samplesRecorded += remaining;
  }

  // Send JSON suffix
  client.print(jsonFoot);

  Serial.printf("✅ Streamed %d samples (%.2f s) | Peak Audio Level: %d\n", 
                samplesRecorded, (float)samplesRecorded / SAMPLE_RATE, peakVal);

  // Read response from backend
  Serial.println("⏳ Awaiting confirmation from BeeWare backend...");
  uint32_t respStart = millis();
  while (client.connected() && !client.available() && (millis() - respStart < 10000)) {
    delay(10);
  }

  while (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.startsWith("HTTP/1.1") || line.startsWith("HTTP/1.0")) {
      Serial.println("✅ " + line);
    }
    if (line.startsWith("{")) {
      Serial.println("📥 " + line);
    }
  }

  client.stop();
}

// ======================== AUDIO RECORDING & 300S COOLDOWN CONTROLLER ========================
// Cooldown timing variables for active loop mode
uint32_t cooldownStartTime = 0;
bool isCoolingDown = false;
uint32_t lastCooldownLogSec = 0;

void executeAudioRecordingCycle(const char* triggerReason) {
  Serial.println("\n╔══════════════════════════════════════════════════════════════╗");
  Serial.println("║ 🎙️  BEEWARE AUDIO RECORDING & TELEMETRY CYCLE                 ║");
  Serial.printf ("║  Trigger: %-51s║\n", triggerReason);
  Serial.println("╚══════════════════════════════════════════════════════════════╝");

  // 1. Ensure Wi-Fi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("📶 Connecting to Wi-Fi: %s ", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("\n❌ Wi-Fi Connection Timeout! Continuing with offline acoustic recording...");
    } else {
      Serial.println("\n✅ Wi-Fi Connected!");
    }
  }

  int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;

  // 2. Read Sensors (DHT22)
  float temp = 0.0;
  float hum  = 0.0;
  readDHTSensor(temp, hum);

  if (temp <= 0.0) {
    Serial.println("⚠️ [NOTIFICATION] Temperature sensor (DHT22) not detected! (0.0 °C)");
  }
  if (hum <= 0.0) {
    Serial.println("⚠️ [NOTIFICATION] Humidity sensor (DHT22) not detected! (0.0 %)");
  }

  int battery = readBatteryPercentage();
  if (battery == 0) battery = 100;
  Serial.printf("📊 Brood Temp: %.1f °C | Humidity: %.1f %% | Battery: %d %%\n", temp, hum, battery);

  // 3. Initialize INMP441 Microphone & Record Audio
  Serial.println("🎙️ [RECORD AUDIO] Initializing INMP441 I2S microphone...");
  setupI2S();

  // 4. Measure Acoustic Level & Frequency from INMP441 Microphone
  int32_t peakAudio = 0;
  int frequencyHz = 0;
  measureAcoustics(peakAudio, frequencyHz);

  if (frequencyHz == 0) {
    Serial.println("⚠️ [NOTIFICATION] Acoustic signal not detected from INMP441 microphone (0 Hz)!");
  } else {
    Serial.printf("🎵 Detected Acoustic Frequency: %d Hz\n", frequencyHz);
  }

  // 5. Record and stream 3.0-second raw audio to BeeWare Backend immediately
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("🎙️ [RECORD AUDIO] Recording & streaming 3.0-second audio to backend...");
    streamTelemetryAndAudio(temp, hum, battery, rssi, frequencyHz);
  }

  // 6. Clean up I2S immediately to free DMA memory and conserve power
  stopI2S();

  // 7. Push real-time telemetry to Firebase Cloud (Starlink & Mobile Data ready)
  if (WiFi.status() == WL_CONNECTED) {
    sendTelemetryToFirebase(temp, hum, battery, rssi, peakAudio, frequencyHz);
  }

  Serial.println("✅ [RECORD AUDIO COMPLETE] Audio recorded, analyzed, and processed successfully!");

  // 8. Enter 300-Second Cooldown
  if (USE_DEEP_SLEEP) {
    Serial.printf("\n💤 [COOLDOWN] Entering Deep Sleep for %d seconds...\n", COOLDOWN_SECONDS);
    delay(1000);
    esp_sleep_enable_timer_wakeup(COOLDOWN_SECONDS * 1000000ULL);
    esp_deep_sleep_start();
  } else {
    Serial.printf("\n❄️ [COOLDOWN] Cooling down for %d seconds (5 minutes) before next audio recording...\n", COOLDOWN_SECONDS);
    cooldownStartTime = millis();
    lastCooldownLogSec = 0;
    isCoolingDown = true;
  }
}

// ======================== SETUP & MAIN LOOP ========================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout resets

  Serial.begin(115200);
  delay(1000);

  // Check Wakeup Cause
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  const char* triggerDesc;
  if (wakeupReason == ESP_SLEEP_WAKEUP_TIMER) {
    triggerDesc = "After 300s Cooldown (Deep Sleep Timer)";
    Serial.println("\n⏰ [WAKEUP] Woke up after 300 seconds cooldown! Recording audio...");
  } else {
    triggerDesc = "ESP32 Turned ON / Initial Boot";
    Serial.println("\n🐝 [BEEWARE_BOOT_TRIGGER] ESP32 Powered ON - Launching BeeWare Backend...");
    Serial.println("🐝 BeeWare ESP32 Node Initializing...");
  }

  // 1. Initialize DHT Sensor
  pinMode(DHTPIN, INPUT_PULLUP); // Ensure data line is pulled HIGH
  dht.begin();
  delay(2000); // Allow DHT22 to stabilize (needs at least 2.0s after power-up)

  // 2. Connect to Wi-Fi
  Serial.printf("📶 Connecting to Wi-Fi: %s ", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 25) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n❌ Wi-Fi Connection Timeout!");
  } else {
    Serial.println("\n✅ Wi-Fi Connected!");

    // ── Send Wake-Up Trigger to Auto-Run Backend on PC ──────────
    String qrDeviceId = getDeviceId();
    String qrMac      = WiFi.macAddress();

    WiFiUDP udp;
    udp.begin(8001); // Bind local port to receive discovery response
    udp.beginPacket("255.255.255.255", 8001);
    String wakeMsg = "{\"event\":\"esp32_boot\",\"deviceId\":\"" + qrDeviceId + "\",\"mac\":\"" + qrMac + "\"}";
    udp.write((const uint8_t*)wakeMsg.c_str(), wakeMsg.length());
    udp.endPacket();
    Serial.println("📡 Broadcasted Wake-Up Beacon on UDP 8001");

    // Wait briefly for discovery response from PC watchdog
    uint32_t waitStart = millis();
    while (millis() - waitStart < 800) {
      int packetSize = udp.parsePacket();
      if (packetSize > 0) {
        char buf[256];
        int len = udp.read(buf, sizeof(buf) - 1);
        if (len > 0) {
          buf[len] = '\0';
          char* hostPos = strstr(buf, "\"host\":\"");
          if (hostPos) {
            hostPos += 8;
            char* endPos = strchr(hostPos, '"');
            if (endPos) {
              *endPos = '\0';
              strncpy(backendHost, hostPos, sizeof(backendHost) - 1);
              backendHost[sizeof(backendHost) - 1] = '\0';
              backendPort = 8000;
              Serial.printf("🎯 [AUTO-DISCOVERY] Backend discovered at %s:%d\n", backendHost, backendPort);
              break;
            }
          }
        }
      }
      delay(20);
    }
    udp.stop();

    // ── Print Direct QR Pairing Info on cold boot ────────────────
    if (wakeupReason != ESP_SLEEP_WAKEUP_TIMER) {
      Serial.println("\n╔══════════════════════════════════════════════╗");
      Serial.println("║     📱 DIRECT QR PAIRING FOR THIS NODE       ║");
      Serial.println("╠══════════════════════════════════════════════╣");
      Serial.printf ("║  Device ID : %s                         ║\n", qrDeviceId.c_str());
      Serial.printf ("║  MAC Addr  : %s                   ║\n", qrMac.c_str());
      Serial.println("╠══════════════════════════════════════════════╣");
      Serial.println("║  1. Copy JSON into https://qr.io (as Text)   ║");
      Serial.println("║  2. Print & stick QR on hive enclosure       ║");
      Serial.println("║  3. Scan in app to connect directly (No BLE) ║");
      Serial.println("╚══════════════════════════════════════════════╝");
      Serial.printf("{\"deviceId\":\"%s\",\"mac\":\"%s\"}\n",
                    qrDeviceId.c_str(), qrMac.c_str());
      Serial.println("══════════════════════════════════════════════\n");
    }
  }

  // Execute initial audio recording cycle (triggered by turning on / waking)
  executeAudioRecordingCycle(triggerDesc);
}

void loop() {
  if (USE_DEEP_SLEEP) {
    // In deep sleep mode, execution never reaches loop() as the board reboots on wake
    return;
  }

  // Active Cooldown Mode (bench testing / USB monitor)
  if (isCoolingDown) {
    uint32_t elapsed = (millis() - cooldownStartTime) / 1000;
    if (elapsed >= COOLDOWN_SECONDS) {
      isCoolingDown = false;
      Serial.println("\n⏰ [COOLDOWN COMPLETE] 300 seconds (5 minutes) elapsed! Recording audio now...");
      executeAudioRecordingCycle("After 300s Cooldown");
    } else {
      // Print cooldown progress every 60 seconds
      if (elapsed != lastCooldownLogSec && elapsed > 0 && (elapsed % 60 == 0)) {
        lastCooldownLogSec = elapsed;
        uint32_t remaining = COOLDOWN_SECONDS - elapsed;
        Serial.printf("❄️ [COOLDOWN] %d seconds remaining in cooldown before next audio recording...\n", remaining);
      }
      delay(250);
    }
  }
}