// TESTE 5: buzzer em A3, ligando e desligando.
// Repete 3 etapas, e a serial diz o que deveria estar acontecendo:
//   1) Tom de 1000 Hz por 1 s, gerado como no jogo (Timer2 + ISR invertendo A3)
//   2) Silencio por 1 s
//   3) A3 em 5V fixo por 1 s (so um buzzer ATIVO apita aqui)
//   4) Silencio por 1 s
// Buzzer passivo: apita na etapa 1 e fica quieto na 3.
// Buzzer ativo: apita na 3 (e na 1 faz um chiado ou um som diferente).
#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#define BUZZER_DDR  DDRC
#define BUZZER_PORT PORTC
#define BUZZER_BIT  PC3   // A3

// Mesmas funcoes do jogo (main.cpp)
void meu_tone(uint16_t frequencia) {
  OCR2A = 62500 / frequencia - 1;
  TCCR2A = (1 << WGM21);
  TCNT2 = 0;
  TIMSK2 = (1 << OCIE2A);
  TCCR2B = (1 << CS22) | (1 << CS20);
}

void meu_noTone() {
  TCCR2B = 0;
  TIMSK2 = 0;
  TCCR2A = 0;
  BUZZER_PORT &= ~(1 << BUZZER_BIT);
}

ISR(TIMER2_COMPA_vect) {
  BUZZER_PORT ^= (1 << BUZZER_BIT);
}

void setup() {
  Serial.begin(9600);
  BUZZER_DDR |= (1 << BUZZER_BIT);
  BUZZER_PORT &= ~(1 << BUZZER_BIT);
  Serial.println(F("TESTE 5 - BUZZER (A3)"));
}

void loop() {
  Serial.println(F("1) LIGADO: tom de 1000 Hz (Timer2, igual ao jogo)"));
  meu_tone(1000);
  delay(1000);

  Serial.println(F("2) DESLIGADO"));
  meu_noTone();
  delay(1000);

  Serial.println(F("3) LIGADO: A3 em 5V fixo (so buzzer ativo apita)"));
  BUZZER_PORT |= (1 << BUZZER_BIT);
  delay(1000);

  Serial.println(F("4) DESLIGADO"));
  BUZZER_PORT &= ~(1 << BUZZER_BIT);
  delay(1000);

  Serial.println();
}
