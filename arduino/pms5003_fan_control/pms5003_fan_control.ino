/*
 * pms5003_fan_control.ino
 * ---------------------------------------------------------------
 * 用 PMS5003 雷射 PM2.5 微粒感測器偵測「抽菸的煙」，
 * 當 PM2.5 濃度超過門檻時，透過繼電器啟動風扇排煙；
 * 空氣變乾淨並維持一段時間後，自動關閉風扇。
 *
 * 為什麼用 PMS5003？香菸的煙本質是懸浮微粒（PM2.5），
 * 用微粒感測器量最直接、最靈敏，也比 MQ 氣體感測器不容易誤判。
 *
 * 硬體（預設）：
 *   - Arduino Uno（或相容板）
 *   - PMS5003 PM2.5 感測器（UART 數位輸出，9600 bps）
 *   - 1 路繼電器模組（低電平觸發，IN 腳 = LOW 時導通/開）
 *   - 風扇（接在繼電器的 NO 端，由獨立電源供電）
 *   - （選配）蜂鳴器，超標時警示
 *
 * ⚠️ 安全提醒：
 *   風扇與繼電器主電路可能是市電（110V/220V），
 *   請務必斷電接線、做好絕緣，不確定時請找有經驗的人協助。
 *   繼電器的 COM/NO/NC 走的是風扇電源，不要接到 Arduino。
 * ---------------------------------------------------------------
 */

#include <SoftwareSerial.h>

/* ====== 接腳設定（依你的接線調整）======
 * PMS5003 是 UART 數位感測器，Uno 只有一組硬體序列埠（給 USB 用），
 * 所以改用 SoftwareSerial 在一般數位腳讀資料。
 * 我們只需要「讀」PMS5003 的資料，所以只接它的 TX。
 */
const int PMS_RX_PIN = 10;   // Arduino 這支腳「接收」PMS5003 的 TX  → 接 PMS5003 的 TX/PIN5
const int PMS_TX_PIN = 11;   // 保留（本程式沒用到，可不接）
const int RELAY_PIN  = 8;    // 繼電器 IN 接到 Arduino D8
const int BUZZER_PIN = 9;    // 蜂鳴器接到 D9（沒有可忽略）
const int LED_PIN    = LED_BUILTIN; // 板載 LED，做狀態指示用

SoftwareSerial pmsSerial(PMS_RX_PIN, PMS_TX_PIN); // (RX, TX)

/* ====== 繼電器觸發電平 ======
 * 大多數藍色小繼電器模組是「低電平觸發」：IN = LOW 時繼電器吸合（風扇開）。
 * 如果你的繼電器是「高電平觸發」，把下面兩行的 LOW/HIGH 對調即可。
 */
const int RELAY_ON  = LOW;   // 讓風扇「開」時，送到 IN 腳的電平
const int RELAY_OFF = HIGH;  // 讓風扇「關」時，送到 IN 腳的電平

/* ====== 門檻與行為設定 ======
 * 單位是 µg/m³（微克/立方公尺）。參考：
 *   良好室內空氣 PM2.5 通常 < 15；有人抽菸時常飆到數十甚至上百。
 * 建議先實際觀察序列埠數值，再依環境微調。
 */
const int PM25_THRESHOLD_ON  = 50;  // 超過此值 → 判定有煙 → 開風扇
const int PM25_THRESHOLD_OFF = 25;  // 低於此值 → 判定乾淨（遲滯設計，避免臨界值反覆開關）

/* 最後一次超標後，還要繼續排風多久才關（毫秒）。避免一有波動就熄火。 */
const unsigned long FAN_KEEP_ON_MS = 15000UL;  // 15 秒

/* PMS5003 上電後風扇/雷射需要一點時間才穩定。 */
const unsigned long STARTUP_MS = 30000UL;      // 建議 30 秒

/* ====== 內部狀態 ====== */
bool fanOn = false;                 // 目前風扇狀態
unsigned long lastSmokeTime = 0;    // 最後一次「超標」的時間點

/* PMS5003 一筆資料是 32 bytes，開頭是 0x42 0x4D。 */
uint8_t frame[32];

void setFan(bool on) {
  fanOn = on;
  digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
  digitalWrite(LED_PIN, on ? HIGH : LOW);
  if (on) tone(BUZZER_PIN, 2000);   // 有煙時嗶嗶警示
  else    noTone(BUZZER_PIN);
}

/*
 * 讀取一筆完整、且校驗和正確的 PMS5003 資料。
 * 成功回傳 PM2.5（大氣環境值，µg/m³）；讀不到或校驗錯誤回傳 -1。
 */
int readPM25() {
  // 找封包開頭 0x42 0x4D
  if (!pmsSerial.available()) return -1;
  if (pmsSerial.read() != 0x42) return -1;

  // 等第二個位元組
  unsigned long t0 = millis();
  while (!pmsSerial.available()) {
    if (millis() - t0 > 100) return -1;
  }
  if (pmsSerial.read() != 0x4D) return -1;

  // 已讀掉前兩個 byte，接著讀剩下的 30 bytes
  frame[0] = 0x42;
  frame[1] = 0x4D;
  for (int i = 2; i < 32; i++) {
    t0 = millis();
    while (!pmsSerial.available()) {
      if (millis() - t0 > 100) return -1;  // 逾時，放棄這筆
    }
    frame[i] = pmsSerial.read();
  }

  // 校驗和：前 30 bytes 相加應等於最後 2 bytes
  uint16_t sum = 0;
  for (int i = 0; i < 30; i++) sum += frame[i];
  uint16_t checksum = (frame[30] << 8) | frame[31];
  if (sum != checksum) return -1;  // 資料損毀

  // PM2.5（大氣環境值）在 frame[12](高位) 與 frame[13](低位)
  int pm25 = (frame[12] << 8) | frame[13];
  return pm25;
}

void setup() {
  Serial.begin(9600);      // 給序列埠監控器看
  pmsSerial.begin(9600);   // PMS5003 固定 9600 bps

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // 先確保風扇關閉
  digitalWrite(RELAY_PIN, RELAY_OFF);
  setFan(false);

  Serial.println(F("=== PMS5003 PM2.5 煙霧偵測風扇控制 ==="));
  Serial.print(F("感測器啟動穩定中，請稍候 "));
  Serial.print(STARTUP_MS / 1000);
  Serial.println(F(" 秒..."));

  unsigned long start = millis();
  while (millis() - start < STARTUP_MS) {
    int pm = readPM25();
    if (pm >= 0) {
      Serial.print(F("啟動中 PM2.5: "));
      Serial.println(pm);
    }
    delay(200);
  }
  Serial.println(F("就緒，開始監測。"));
}

void loop() {
  int pm25 = readPM25();

  if (pm25 >= 0) {           // 只有成功讀到才判斷
    // 超標 → 記錄時間、確保風扇開啟
    if (pm25 >= PM25_THRESHOLD_ON) {
      lastSmokeTime = millis();
      if (!fanOn) {
        Serial.println(F(">> 偵測到煙霧（PM2.5 超標），啟動風扇排煙！"));
        setFan(true);
      }
    }

    // 風扇開著、目前已低於乾淨門檻、且距最後一次超標已超過保持時間 → 關風扇
    if (fanOn &&
        pm25 <= PM25_THRESHOLD_OFF &&
        (millis() - lastSmokeTime >= FAN_KEEP_ON_MS)) {
      Serial.println(F(">> 空氣已恢復乾淨，關閉風扇。"));
      setFan(false);
    }

    // 狀態輸出
    Serial.print(F("PM2.5: "));
    Serial.print(pm25);
    Serial.print(F(" ug/m3  風扇: "));
    Serial.println(fanOn ? F("ON") : F("OFF"));
  }

  delay(1000);  // 每秒偵測一次（PMS5003 約每秒送一筆）
}
