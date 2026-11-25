// TESTE 4: LEDs, buzzer, potenciometros, fios e botoes.
// - Pisca os LEDs D13 (vermelho), D12 (verde) e D11 (amarelo), um por vez
// - Bipa o buzzer em A3 uma vez por ciclo
// - Mostra na serial A0 e A1 (cofre), A2 (fios) e os botoes D2 e D10
// Gire os potenciometros: os valores devem ir de 0 a 1023.
// Aperte os botoes: devem mostrar 1 (pull-down externo, botao ligado ao 5V).
#include <Arduino.h>

const byte leds[3] = {13, 12, 11};

void setup() {
  Serial.begin(9600);
  for (byte i = 0; i < 3; i++) pinMode(leds[i], OUTPUT);
  pinMode(A3, OUTPUT);
  pinMode(2, INPUT);
  pinMode(10, INPUT);
  Serial.println(F("TESTE 4 - COMPONENTES"));
}

void loop() {
  static byte passo = 0;

  for (byte i = 0; i < 3; i++) digitalWrite(leds[i], i == passo % 3 ? HIGH : LOW);

  if (passo % 3 == 0) {
    tone(A3, 1000, 100);
  }

  Serial.print(F("LED aceso: D"));
  Serial.print(leds[passo % 3]);
  Serial.print(F(" | A0=")); Serial.print(analogRead(A0));
  Serial.print(F(" A1="));   Serial.print(analogRead(A1));
  Serial.print(F(" A2="));   Serial.print(analogRead(A2));
  Serial.print(F(" | botao D2=")); Serial.print(digitalRead(2));
  Serial.print(F(" D10="));        Serial.println(digitalRead(10));

  passo++;
  delay(500);
}
