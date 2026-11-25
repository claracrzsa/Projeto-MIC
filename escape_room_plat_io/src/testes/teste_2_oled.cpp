// TESTE 2: escreve no OLED com o mesmo layout 16x2 usado pelo jogo.
// Se o endereco do teste 1 foi 0x3D, mude OLED_ADDR abaixo e no main.cpp.
#include <Arduino.h>
#include <Wire.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiWire.h"

#define OLED_ADDR 0x3C

SSD1306AsciiWire oled;

void setup() {
  Serial.begin(9600);
  Wire.begin();
  Wire.setClock(400000L);

  oled.begin(&Adafruit128x64, OLED_ADDR);
  oled.setFont(ZevvPeep8x16);
  oled.setLetterSpacing(1);
  oled.clear();

  oled.setCursor(0, 0);
  oled.print(F("0123456789ABCDEF"));   // linha 0: precisa caber 16 colunas
  oled.setCursor(0, 4);
  oled.print(F("MODO: FACIL"));         // linha 1
  oled.setCursor(12 * 8, 4);
  oled.print(F("3:00"));

  Serial.println(F("TESTE 2 - OLED"));
  Serial.println(F("Linha de cima: 0123456789ABCDEF (16 caracteres inteiros)"));
  Serial.println(F("Linha do meio: MODO: FACIL 3:00, contando para baixo"));
}

void loop() {
  static unsigned int tempo = 180;

  oled.setCursor(12 * 8, 4);
  oled.print(tempo / 60);
  oled.print(':');
  oled.print((tempo % 60) / 10);
  oled.print(tempo % 10);

  if (tempo > 0) tempo--;
  delay(1000);
}
