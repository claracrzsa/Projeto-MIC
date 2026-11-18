#include <avr/io.h>
// macros de bit

#define set_bit(reg, bit) ((reg) |= (1 << (bit)))
#define clr_bit(reg, bit) ((reg) &= ~(1 << (bit)))
#define cpl_bit(reg, bit) ((reg) ^= (1 << (bit)))
#define tst_bit(reg, bit) ((reg) & (1 << (bit)))

void meu_tone(uint16_t frequencia) {
  // tone(11, frequencia), frequencia >= 245 Hz
  // f = 16 MHz / (2 * 128 * (OCR2A + 1))
  OCR2A = 62500 / frequencia - 1;
  // modo CTC, D11 (OC2A) inverte a cada volta
  TCCR2A = (1 << COM2A0) | (1 << WGM21);
  TCCR2B = (1 << CS22) | (1 << CS20); // N = 128: liga
}

void meu_noTone() { // noTone(11)
  TCCR2B = 0; // para o timer
  TCCR2A = 0; // D11 volta a ser pino comum
  clr_bit(PORTB, PB3);
}

void setup() {
  set_bit(DDRB, PB3); // pinMode(11, OUTPUT)
}
void loop() {
  meu_tone(440);
  delay(200);
  meu_noTone();
  delay(800);
}
