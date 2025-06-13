#include <M5StickC.h>
#include <Ticker.h>

Ticker ticker;

//0.1秒毎にアスキーコードの送信
void sendByte() {
  static uint8_t i = 0x21;

  if(i >= 0x7e){
    i = 0x0d;
    Serial.write(i);
    i = 0x0a;
    Serial.write(i);
    i = 0x21;
  }else{
    i++;
  }
  Serial.write(i);
}

uint8_t lastReceived = 0;

void setup() {
  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(ORANGE, BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Teriyaki Monitor");

  Serial.begin(115200);

  // 1秒ごとに sendByte() を呼び出す
  ticker.attach(0.1, sendByte);
}

void loop() {
  // UART受信して内容をLCDに表示
  while (Serial.available() > 0) {
    lastReceived = Serial.read();
    M5.Lcd.fillRect(0, 30, 160, 30, BLACK);
    M5.Lcd.setCursor(0, 30);
    M5.Lcd.printf("RX: 0x%02X", lastReceived);
  }
}
