// TESTE 1: procura os dispositivos no barramento I2C (A4 = SDA, A5 = SCL).
// Esperado: 0x3C (OLED, as vezes 0x3D) e 0x70 (display de 7 segmentos HT16K33).
#include <Arduino.h>
#include <Wire.h>

void setup() {
  Serial.begin(9600);
  Wire.begin();
  Serial.println(F("TESTE 1 - I2C"));
}

void loop() {
  byte encontrados = 0;

  Serial.println(F("Procurando..."));

  for (byte endereco = 1; endereco < 127; endereco++) {
    Wire.beginTransmission(endereco);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("  encontrado: 0x"));
      if (endereco < 16) Serial.print('0');
      Serial.print(endereco, HEX);
      if (endereco == 0x3C || endereco == 0x3D) Serial.print(F("  (OLED)"));
      if (endereco == 0x70) Serial.print(F("  (display 7 segmentos HT16K33)"));
      Serial.println();
      encontrados++;
    }
  }

  if (encontrados == 0) {
    Serial.println(F("  NADA encontrado: confira SDA em A4, SCL em A5, VCC e GND"));
  }

  delay(3000);
}
