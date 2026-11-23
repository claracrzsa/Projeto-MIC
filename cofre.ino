#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <stdint.h>

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

// 1. MACROS PARA OPERACOES DE BITS

#define set_bit(reg, bit) ((reg) |= (1 << (bit)))
#define clr_bit(reg, bit) ((reg) &= ~(1 << (bit)))
#define tst_bit(reg, bit) ((reg) & (1 << (bit)))

// 2. DEFINICAO DOS PINOS

// LEDs indicadores de cada cofre
// ATENCAO: PD6 tambem e o LED1 do desafio da sequencia.
// Se os dois desafios rodarem no mesmo microcontrolador, mude um deles (ex.: PD7).
#define LED_COFRE1 PD5
#define LED_COFRE2 PD6

// 3. PARAMETROS DO COFRE

// Faixa correta de cada potenciometro (valor do ADC, 0 a 1023):
// o LED acende quando a leitura esta ENTRE o limiar baixo e o alto.
#define LIMIAR_BAIXO1 600
#define LIMIAR_ALTO1  700

#define LIMIAR_BAIXO2 300
#define LIMIAR_ALTO2  400

// Histerese: depois de acender, so apaga se sair da faixa por mais que isso
#define HISTERESE 10

// Numero de amostras para a media (reduz ruido do ADC)
#define N_AMOSTRAS 8

// Tempo que os dois cofres precisam ficar na posicao certa para abrir
#define TEMPO_ESTAVEL_MS 1000

// Intervalo entre mensagens de acompanhamento na serial
#define INTERVALO_LOG_MS 250

// UART
#define BAUD 9600UL
#define UBRR_VALOR ((F_CPU / 16UL / BAUD) - 1)   // 103 para 9600 baud a 16 MHz

// 4. VARIAVEIS

volatile uint16_t tempo_ms = 0;   // contador livre, 1 ms por tick

uint16_t leitura_A0 = 0;
uint16_t leitura_A1 = 0;

uint16_t soma_A0 = 0;
uint16_t soma_A1 = 0;
uint8_t amostras_A0 = 0;
uint8_t amostras_A1 = 0;

uint8_t canal = 0;
uint8_t acender_led1 = 0;
uint8_t acender_led2 = 0;

bool desafio_concluido = false;   // vira true quando o cofre abre
bool contando_estavel = false;
uint16_t inicio_estavel = 0;
uint16_t ultimo_log = 0;

// 5. INTERRUPCAO DO TIMER1 (1 ms)

ISR(TIMER1_COMPA_vect) {
  tempo_ms++;
}

// 6. TEMPO (SEM BLOQUEIO)

// Le o contador de forma atomica (e de 16 bits)
uint16_t agora_ms() {
  uint16_t t;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    t = tempo_ms;
  }

  return t;
}

// 7. UART POR REGISTRADORES (somente transmissao)

void uart_init() {
  UBRR0H = (uint8_t)(UBRR_VALOR >> 8);
  UBRR0L = (uint8_t)(UBRR_VALOR);

  UCSR0A = 0;

  // Habilita apenas o transmissor
  UCSR0B = (1 << TXEN0);

  // Formato 8N1
  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void uart_enviar_char(char c) {
  while (!tst_bit(UCSR0A, UDRE0)) {
  }

  UDR0 = c;
}

void uart_enviar_texto(const char *texto) {
  while (*texto) {
    uart_enviar_char(*texto);
    texto++;
  }
}

void uart_enviar_numero(uint16_t n) {
  char digitos[5];
  uint8_t i = 0;

  if (n == 0) {
    uart_enviar_char('0');
    return;
  }

  while (n > 0 && i < 5) {
    digitos[i++] = '0' + (n % 10);
    n /= 10;
  }

  while (i > 0) {
    uart_enviar_char(digitos[--i]);
  }
}

void uart_enviar_linha(const char *texto) {
  uart_enviar_texto(texto);
  uart_enviar_texto("\r\n");
}

// 8. LOGICA DOS COFRES

// Liga quando o valor entra na faixa (baixo, alto).
// Depois de ligado, so desliga quando sai da faixa alargada pela histerese.
uint8_t calcula_estado(uint16_t valor, uint16_t baixo, uint16_t alto, uint8_t estado_atual) {
  if (estado_atual) {
    if (valor + HISTERESE <= baixo || valor >= alto + HISTERESE) {
      return 0;
    }
    return 1;
  }

  if (valor > baixo && valor < alto) {
    return 1;
  }

  return 0;
}

void atualiza_led1(uint16_t valor) {
  acender_led1 = calcula_estado(valor, LIMIAR_BAIXO1, LIMIAR_ALTO1, acender_led1);

  if (acender_led1) {
    set_bit(PORTD, LED_COFRE1);
  } else {
    clr_bit(PORTD, LED_COFRE1);
  }
}

void atualiza_led2(uint16_t valor) {
  acender_led2 = calcula_estado(valor, LIMIAR_BAIXO2, LIMIAR_ALTO2, acender_led2);

  if (acender_led2) {
    set_bit(PORTD, LED_COFRE2);
  } else {
    clr_bit(PORTD, LED_COFRE2);
  }
}

// 9. CONFIGURACAO INICIAL

void setup() {
  uart_init();

  // LEDs como saidas, inicialmente apagados
  set_bit(DDRD, LED_COFRE1);
  set_bit(DDRD, LED_COFRE2);
  clr_bit(PORTD, LED_COFRE1);
  clr_bit(PORTD, LED_COFRE2);

  // Configuracao do Timer1: CTC, prescaler 64, 1 ms a 16 MHz
  cli();
  TCCR1A = 0;
  TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10);
  OCR1A = 249;
  TCNT1 = 0;
  TIMSK1 = (1 << OCIE1A);
  sei();

  ADMUX = (1 << REFS0); // Referencia 5V, canal 0
                        // Clock: 16 / 128
  ADCSRA = (1 << ADEN)
         | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
  set_bit(ADCSRA, ADSC); // Primeira conversao
}

// 10. EXECUTAR O DESAFIO

void loop() {

  // Cofre ja aberto: nada a fazer, loop livre para o proximo desafio
  if (desafio_concluido) {
    return;
  }

  // Leitura do ADC, um canal por vez, sem bloqueio
  if (!tst_bit(ADCSRA, ADSC)) // Conversao terminou?
  {
    switch (canal)
    {
      case 0:
        soma_A0 += ADC;
        amostras_A0++;

        if (amostras_A0 >= N_AMOSTRAS) {
          leitura_A0 = soma_A0 / N_AMOSTRAS;
          soma_A0 = 0;
          amostras_A0 = 0;
        }

        canal = 1;
        break;

      case 1:
        soma_A1 += ADC;
        amostras_A1++;

        if (amostras_A1 >= N_AMOSTRAS) {
          leitura_A1 = soma_A1 / N_AMOSTRAS;
          soma_A1 = 0;
          amostras_A1 = 0;
        }

        canal = 0;
        break;
    }

    ADMUX = (1 << REFS0) | canal; // Seleciona proximo canal

    set_bit(ADCSRA, ADSC); // Inicia proxima conversao
  }

  atualiza_led1(leitura_A0);
  atualiza_led2(leitura_A1);

  // Os dois cofres na posicao certa: precisa manter por TEMPO_ESTAVEL_MS
  if (acender_led1 && acender_led2) {

    if (!contando_estavel) {
      contando_estavel = true;
      inicio_estavel = agora_ms();
    }
    else if ((uint16_t)(agora_ms() - inicio_estavel) >= TEMPO_ESTAVEL_MS) {
      desafio_concluido = true;   // LEDs permanecem acesos
      uart_enviar_linha("COFRE ABERTO");
      return;
    }

  } else {
    contando_estavel = false;
  }

  // Acompanhamento na serial, a cada INTERVALO_LOG_MS (sem delay)
  uint16_t agora = agora_ms();

  if ((uint16_t)(agora - ultimo_log) >= INTERVALO_LOG_MS) {
    ultimo_log = agora;

    uart_enviar_texto("A0=");
    uart_enviar_numero(leitura_A0);
    uart_enviar_texto(" A1=");
    uart_enviar_numero(leitura_A1);
    uart_enviar_texto("\r\n");
  }
}
