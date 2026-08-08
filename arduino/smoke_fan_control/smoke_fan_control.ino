/*
 * smoke_fan_control.ino
 * ---------------------------------------------------------------
 * 用 MQ-2 煙霧 / 可燃氣體感測器偵測「煙味」，
 * 當濃度超過門檻時，透過繼電器啟動風扇排煙；
 * 濃度回到安全範圍並維持一段時間後，自動關閉風扇。
 *
 * 硬體（預設）：
 *   - Arduino Uno（或相容板）
 *   - MQ-2 煙霧感測器模組（有 A0 類比輸出腳）
 *   - 1 路繼電器模組（低電平觸發，IN 腳 = LOW 時導通/開）
 *   - 風扇（接在繼電器的 NO 端，由獨立電源供電）
 *   - （選配）蜂鳴器，煙霧超標時警示
 *
 * ⚠️ 安全提醒：
 *   風扇與繼電器主電路可能是市電（110V/220V），
 *   請務必斷電接線、做好絕緣，不確定時請找有經驗的人協助。
 *   繼電器的 COM/NO/NC 走的是風扇電源，不要接到 Arduino。
 * ---------------------------------------------------------------
 */

/* ====== 接腳設定（依你的接線調整）====== */
const int MQ2_ANALOG_PIN = A0;   // MQ-2 的 A0（類比輸出）接到 Arduino A0
const int RELAY_PIN      = 8;    // 繼電器 IN 接到 Arduino D8
const int BUZZER_PIN     = 9;    // 蜂鳴器接到 D9（沒有可忽略）
const int LED_PIN        = LED_BUILTIN; // 板載 LED，做狀態指示用

/* ====== 繼電器觸發電平 ======
 * 大多數藍色小繼電器模組是「低電平觸發」：IN = LOW 時繼電器吸合（風扇開）。
 * 如果你的繼電器是「高電平觸發」，把下面兩行的 LOW/HIGH 對調即可。
 */
const int RELAY_ON  = LOW;   // 讓風扇「開」時，送到 IN 腳的電平
const int RELAY_OFF = HIGH;  // 讓風扇「關」時，送到 IN 腳的電平

/* ====== 門檻與行為設定 ======
 * MQ-2 類比值範圍 0~1023，數值越大代表偵測到的煙霧/氣體越濃。
 * 每顆感測器、每個環境的基準值都不同，請用序列埠監控器
 * （Serial Monitor，鮑率 9600）觀察「乾淨空氣」時的數值，
 * 再把門檻設在它之上一段安全距離。
 */
const int SMOKE_THRESHOLD_ON  = 400;  // 超過此值 → 判定有煙 → 開風扇
const int SMOKE_THRESHOLD_OFF = 300;  // 低於此值 → 判定安全（遲滯設計，避免臨界值反覆開關）

/* 最後一次偵測到煙後，還要繼續排風多久才關（毫秒）。避免一有波動就熄火。 */
const unsigned long FAN_KEEP_ON_MS = 10000UL;  // 10 秒

/* MQ-2 需要暖機才會穩定，剛上電讀值會偏高。 */
const unsigned long WARMUP_MS = 20000UL;       // 建議 20 秒以上（正式使用可設更久）

/* ====== 內部狀態 ====== */
bool fanOn = false;                 // 目前風扇狀態
unsigned long lastSmokeTime = 0;    // 最後一次偵測到「有煙」的時間點

void setFan(bool on) {
  fanOn = on;
  digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
  digitalWrite(LED_PIN, on ? HIGH : LOW);
  // 有煙時嗶嗶警示，安全時安靜
  if (on) tone(BUZZER_PIN, 2000);
  else    noTone(BUZZER_PIN);
}

void setup() {
  Serial.begin(9600);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // 先確保風扇關閉（先設定電平再算開機狀態，避免上電瞬間亂動）
  digitalWrite(RELAY_PIN, RELAY_OFF);
  setFan(false);

  Serial.println(F("=== MQ-2 煙霧偵測風扇控制 ==="));
  Serial.print(F("感測器暖機中，請稍候 "));
  Serial.print(WARMUP_MS / 1000);
  Serial.println(F(" 秒..."));

  // 暖機：期間不控制風扇，只讓感測器穩定
  unsigned long start = millis();
  while (millis() - start < WARMUP_MS) {
    Serial.print(F("暖機讀值: "));
    Serial.println(analogRead(MQ2_ANALOG_PIN));
    delay(1000);
  }
  Serial.println(F("暖機完成，開始監測。"));
}

void loop() {
  int smokeValue = analogRead(MQ2_ANALOG_PIN);  // 讀取煙霧濃度（0~1023）

  // 偵測到煙 → 記錄時間，並確保風扇開啟
  if (smokeValue >= SMOKE_THRESHOLD_ON) {
    lastSmokeTime = millis();
    if (!fanOn) {
      Serial.println(F(">> 偵測到煙霧，啟動風扇排煙！"));
      setFan(true);
    }
  }

  // 風扇開著、目前已低於安全門檻、且距離最後一次偵測到煙已超過保持時間 → 關風扇
  if (fanOn &&
      smokeValue <= SMOKE_THRESHOLD_OFF &&
      (millis() - lastSmokeTime >= FAN_KEEP_ON_MS)) {
    Serial.println(F(">> 煙霧已消散，關閉風扇。"));
    setFan(false);
  }

  // 狀態輸出
  Serial.print(F("煙霧值: "));
  Serial.print(smokeValue);
  Serial.print(F("  風扇: "));
  Serial.println(fanOn ? F("ON") : F("OFF"));

  delay(500);  // 每 0.5 秒偵測一次
}
