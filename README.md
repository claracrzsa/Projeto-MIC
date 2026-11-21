#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <util/delay.h>
#include <Wire.h>

// MACROS
#define set_bit(reg, bit) ((reg) |= (1 << (bit)))
#define clr_bit(reg, bit) ((reg) &= ~(1 << (bit)))
#define cpl_bit(reg, bit) ((reg) ^= (1 << (bit)))
#define tst_bit(reg, bit) ((reg) & (1 << (bit)))

// DISPLAY
const uint8_t endereco = 0x70;

const uint8_t numeros[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66,
  0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

// TIMER
volatile uint32_t ms = 0;

uint32_t inicio = 0;
uint32_t penalidade_ms = 0;

int ultimo = -1;
bool encerrado = false;

// SEMENTE ALEATORIA
uint32_t semente_aleatoria = 123456789UL;

// FIOS SUPERIORES
// 0 = vermelho (D13)
// 1 = verde (D12)
// 2 = amarelo (D11)

const uint8_t fios_cima[3] = {
  PB5, PB4, PB3
};

// CONTROLE DAS CONEXOES
uint8_t candidato[3] = {255, 255, 255};
uint32_t inicio_contato[3] = {0};

bool registrado[3][3] = {};

// INTERRUPCAO A CADA 1 ms
ISR(TIMER1_COMPA_vect) {
  ms++;
}

// LEITURA SEGURA DO TEMPO
uint32_t ler_tempo_ms() {
  uint32_t tempo;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    tempo = ms;
  }

  return tempo;
}

// CONFIGURACAO DOS PINOS
void configurar_pinos() {
  set_bit(DDRB, PB3);
  set_bit(DDRB, PB4);
  set_bit(DDRB, PB5);

  clr_bit(PORTB, PB3);
  clr_bit(PORTB, PB4);
  clr_bit(PORTB, PB5);

  clr_bit(DDRC, PC1);
  clr_bit(PORTC, PC1);
}

// LIGA OS LEDS
void ligar_leds() {
  set_bit(PORTB, PB3);
  set_bit(PORTB, PB4);
  set_bit(PORTB, PB5);
}

// DESLIGA OS LEDS
void desligar_leds() {
  clr_bit(PORTB, PB3);
  clr_bit(PORTB, PB4);
  clr_bit(PORTB, PB5);
}

// CONFIGURACAO DO TIMER1
void configurar_timer() {
  cli();

  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;

  OCR1A = 249;

  set_bit(TCCR1B, WGM12);
  set_bit(TCCR1B, CS11);
  set_bit(TCCR1B, CS10);
  set_bit(TIMSK1, OCIE1A);

  sei();
}

// CONFIGURACAO DO ADC
void configurar_adc() {
  ADMUX = (1 << REFS0) | (1 << MUX0);

  ADCSRA = (1 << ADEN)
          | (1 << ADPS2)
          | (1 << ADPS1)
          | (1 << ADPS0);
}

// LEITURA DO ADC
uint16_t ler_adc() {
  set_bit(ADCSRA, ADSC);

  while (tst_bit(ADCSRA, ADSC)) {
  }

  uint16_t valor = ADC;

  semente_aleatoria =
      semente_aleatoria * 1664525UL
      + 1013904223UL
      + valor
      + TCNT1;

  return valor;
}

// COMANDO DO DISPLAY
void comando(uint8_t valor) {
  Wire.beginTransmission(endereco);
  Wire.write(valor);
  Wire.endTransmission();
}

// CONFIGURACAO DO DISPLAY
void configurar_display() {
  Wire.begin();

  comando(0x21);
  comando(0x81);
  comando(0xEF);
}

// MOSTRA O TEMPO
void mostrar(int tempo) {
  uint8_t dados[16] = {0};

  dados[6] = numeros[tempo / 10];
  dados[8] = numeros[tempo % 10];

  Wire.beginTransmission(endereco);
  Wire.write(0x00);

  for (uint8_t i = 0; i < 16; i++) {
    Wire.write(dados[i]);
  }

  Wire.endTransmission();
}

// MOSTRA O RESULTADO
void mostrar_resultado(uint8_t numero) {
  uint8_t dados[16] = {0};

  dados[8] = numeros[numero];

  Wire.beginTransmission(endereco);
  Wire.write(0x00);

  for (uint8_t i = 0; i < 16; i++) {
    Wire.write(dados[i]);
  }

  Wire.endTransmission();
}

// IDENTIFICA O FIO INFERIOR
// 0 = vermelho
// 1 = verde
// 2 = amarelo
// 255 = nenhum

uint8_t identificar_fio(uint16_t valor) {

  // VERMELHO
  if (valor >= 200 && valor <= 230) {
    return 0;
  }

  // VERDE
  if (valor >= 95 && valor <= 125) {
    return 1;
  }

  // AMARELO
  if (valor >= 170 && valor <= 195) {
    return 2;
  }

  return 255;
}

// TESTA UM FIO SUPERIOR
uint8_t testar_fio(uint8_t cima) {

  desligar_leds();

  set_bit(PORTB, fios_cima[cima]);

  _delay_us(200);

  ler_adc();

  uint16_t valor = ler_adc();

  ligar_leds();

  return identificar_fio(valor);
}

// APLICA A PENALIDADE
void aplicar_regra(uint8_t cima, uint8_t baixo) {

  if (cima == 0 && baixo == 2) {
    penalidade_ms += 5000UL;
  }
}

// VERIFICA OS FIOS DURANTE O JOGO
void verificar_fios(uint32_t agora) {

  for (uint8_t c = 0; c < 3; c++) {

    uint8_t baixo = testar_fio(c);

    if (baixo == 255) {
      candidato[c] = 255;
      continue;
    }

    if (candidato[c] != baixo) {

      candidato[c] = baixo;
      inicio_contato[c] = agora;
    }

    if (agora - inicio_contato[c] >= 40UL) {

      if (!registrado[c][baixo]) {

        registrado[c][baixo] = true;
        aplicar_regra(c, baixo);
      }
    }
  }
}

// GERA NUMERO ALEATORIO DE 1 A 9
uint8_t gerar_numero_aleatorio() {

  semente_aleatoria ^= semente_aleatoria << 13;
  semente_aleatoria ^= semente_aleatoria >> 17;
  semente_aleatoria ^= semente_aleatoria << 5;

  return (semente_aleatoria % 9) + 1;
}

// FINALIZA O JOGO E TESTA OS FIOS
void finalizar_jogo() {

  bool todos_corretos = true;

  Serial.println("===== TESTE DOS FIOS =====");

  for (uint8_t c = 0; c < 3; c++) {

    desligar_leds();

    set_bit(PORTB, fios_cima[c]);

    _delay_us(200);

    ler_adc();

    uint16_t valor = ler_adc();

    uint8_t baixo = identificar_fio(valor);

    ligar_leds();

    Serial.print("Fio superior: ");
    Serial.print(c);

    Serial.print(" | Valor A0: ");
    Serial.print(valor);

    Serial.print(" | Identificacao: ");
    Serial.println(baixo);

    // CONFERE SE O FIO ESTA NA COR CORRETA
    if (baixo != c) {
      todos_corretos = false;
    }

    delay(1000);
  }

  Serial.println("===== FIM DO TESTE =====");

  // SE TODOS OS FIOS ESTIVEREM CORRETOS
  if (todos_corretos) {

    uint8_t resultado = gerar_numero_aleatorio();

    Serial.print("TODOS OS FIOS CORRETOS! NUMERO: ");
    Serial.println(resultado);

    mostrar_resultado(resultado);

  } else {

    Serial.println("FIO INCORRETO!");

    mostrar_resultado(0);
  }

  // MANTEM OS TRES LEDS LIGADOS
  DDRB |= (1 << PB3) | (1 << PB4) | (1 << PB5);

  PORTB |= (1 << PB3) | (1 << PB4) | (1 << PB5);

  encerrado = true;

  // MANTEM OS LEDS LIGADOS PERMANENTEMENTE
  while (1) {
    PORTB |= (1 << PB3) | (1 << PB4) | (1 << PB5);
  }
}

// CONFIGURACAO INICIAL
void setup() {

  Serial.begin(9600);

  configurar_pinos();
  configurar_adc();
  configurar_display();
  configurar_timer();

  mostrar(5);
  ligar_leds();

  inicio = ler_tempo_ms();
}

// PROGRAMA PRINCIPAL
void loop() {

  if (encerrado) {
    return;
  }

  uint32_t agora = ler_tempo_ms();

  verificar_fios(agora);

  uint32_t decorrido =
      agora - inicio + penalidade_ms;

  if (decorrido >= 5000UL) {

    finalizar_jogo();

    return;
  }

  int tempo = 5 - (decorrido / 1000UL);

  if (tempo != ultimo) {

    mostrar(tempo);
    ultimo = tempo;
  }
}
