#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

// 1. MACROS PARA OPERACOES DE BITS

#define set_bit(reg, bit) ((reg) |= (1 << (bit)))
#define clr_bit(reg, bit) ((reg) &= ~(1 << (bit)))
#define cpl_bit(reg, bit) ((reg) ^= (1 << (bit)))
#define tst_bit(reg, bit) ((reg) & (1 << (bit)))

// 2. DEFINICAO DOS PINOS

// LEDs
#define LED1 PD6
#define LED2 PD4

// Botoes
#define BOTAO1 PD2
#define BOTAO2 PD3

// Tipo de ligacao dos botoes:
// 1 = botao liga o pino ao 5V, com resistor pull-down externo (le 1 ao pressionar)
// 0 = botao liga o pino ao GND, usa pull-up interno (le 0 ao pressionar)
#define PULLDOWN_EXTERNO 1

#if PULLDOWN_EXTERNO
  #define PRESSIONADO(pino) (tst_bit(PIND, pino) != 0)
#else
  #define PRESSIONADO(pino) (tst_bit(PIND, pino) == 0)
#endif

// UART
#define BAUD 9600UL
#define UBRR_VALOR ((F_CPU / 16UL / BAUD) - 1)   // 103 para 9600 baud a 16 MHz

// Comando serial de reinicio
#define COMANDO_REINICIO 'R'

// 3. VARIAVEIS

volatile uint8_t ms = 0;
volatile bool reiniciar_solicitado = false;   // alterada na ISR da UART

bool desafio_concluido = false;

// Sequencia:
// 1 = LED1 / BOTAO1
// 2 = LED2 / BOTAO2
const uint8_t sequencia[] = {1, 2, 2, 1, 2};
const uint8_t TAMANHO = sizeof(sequencia);

// 4. INTERRUPCOES

// Timer1: base de tempo de 1 ms
ISR(TIMER1_COMPA_vect) {
  ms++;
}

// UART: recepcao de caractere. So sinaliza a flag de reinicio.
ISR(USART_RX_vect) {
  char c = UDR0;

  if (c == COMANDO_REINICIO || c == (COMANDO_REINICIO + 32)) {   // 'R' ou 'r'
    reiniciar_solicitado = true;
  }
}

// 5. FUNCAO DE ESPERA

void espera_ms(uint16_t tempo) {
  while (tempo > 0) {
    uint8_t intervalo = tempo > 200 ? 200 : tempo;
    uint8_t inicio = ms;

    while ((uint8_t)(ms - inicio) < intervalo) {
      // Timer continua contando
    }

    tempo -= intervalo;
  }
}

// 6. UART POR REGISTRADORES

void uart_init() {
  // Baud rate
  UBRR0H = (uint8_t)(UBRR_VALOR >> 8);
  UBRR0L = (uint8_t)(UBRR_VALOR);

  // Modo assincrono normal (sem U2X)
  UCSR0A = 0;

  // Habilita receptor, transmissor e interrupcao de recepcao
  UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);

  // Formato 8N1: 8 bits de dados, sem paridade, 1 stop bit
  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void uart_enviar_char(char c) {
  // Espera o registrador de transmissao ficar livre
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

void uart_enviar_numero(uint8_t n) {
  if (n >= 100) {
    uart_enviar_char('0' + (n / 100));
  }
  if (n >= 10) {
    uart_enviar_char('0' + ((n / 10) % 10));
  }
  uart_enviar_char('0' + (n % 10));
}

void uart_enviar_linha(const char *texto) {
  uart_enviar_texto(texto);
  uart_enviar_texto("\r\n");
}

void uart_enviar_linha_numero(const char *rotulo, uint8_t valor) {
  uart_enviar_texto(rotulo);
  uart_enviar_numero(valor);
  uart_enviar_texto("\r\n");
}

// 7. CONFIGURACAO INICIAL

void setup() {
  uart_init();

  // LEDs como saidas
  set_bit(DDRD, LED1);
  set_bit(DDRD, LED2);

  // Botoes como entradas
  clr_bit(DDRD, BOTAO1);
  clr_bit(DDRD, BOTAO2);

#if PULLDOWN_EXTERNO
  // Pull-up interno desligado (usa pull-down externo)
  clr_bit(PORTD, BOTAO1);
  clr_bit(PORTD, BOTAO2);
#else
  // Pull-up interno ligado (botao vai ao GND)
  set_bit(PORTD, BOTAO1);
  set_bit(PORTD, BOTAO2);
#endif

  // LEDs inicialmente apagados
  clr_bit(PORTD, LED1);
  clr_bit(PORTD, LED2);

  // Configuracao do Timer1: CTC, prescaler 64, 1 ms a 16 MHz
  cli();
  TCCR1A = 0;
  TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10);
  OCR1A = 249;
  TCNT1 = 0;
  TIMSK1 = (1 << OCIE1A);
  sei();
}

// 8. APAGAR OS DOIS LEDS

void apagar_leds() {
  clr_bit(PORTD, LED1);
  clr_bit(PORTD, LED2);
}

// 9. ACENDER O LED CORRESPONDENTE

void acender_led(uint8_t valor) {
  if (valor == 1) {
    set_bit(PORTD, LED1);
  } else if (valor == 2) {
    set_bit(PORTD, LED2);
  }
}

// 10. APRESENTAR A SEQUENCIA

void apresentar_sequencia() {
  // Acende os dois LEDs simultaneamente
  acender_led(1);
  acender_led(2);
  espera_ms(300);
  apagar_leds();
  espera_ms(300);

  // Apresenta a sequencia individualmente
  for (uint8_t i = 0; i < TAMANHO; i++) {
    acender_led(sequencia[i]);
    espera_ms(300);
    apagar_leds();
    espera_ms(300);
  }
}

// 11. AGUARDAR A LIBERACAO DOS BOTOES

void aguardar_soltar() {
  while (PRESSIONADO(BOTAO1) || PRESSIONADO(BOTAO2)) {
  }

  // Elimina o repique na liberacao
  espera_ms(30);

  while (PRESSIONADO(BOTAO1) || PRESSIONADO(BOTAO2)) {
  }
}

// 12. REINICIAR O DESAFIO

void reiniciar_desafio() {
  apagar_leds();
  desafio_concluido = false;
  reiniciar_solicitado = false;

  uart_enviar_linha("Desafio reiniciado");
}

// 13. LER QUAL BOTAO FOI PRESSIONADO
// Retorna 1 (BOTAO1), 2 (BOTAO2) ou 0 se o reinicio foi solicitado

uint8_t ler_botao() {

  while (1) {

    uint8_t botao1 = PRESSIONADO(BOTAO1);
    uint8_t botao2 = PRESSIONADO(BOTAO2);

    // Nenhum botao pressionado: confere se pediram reinicio
    if (!botao1 && !botao2) {
      if (reiniciar_solicitado) {
        return 0;   // leitura abortada por pedido de reinicio
      }

      continue;
    }

    // Aguarda para eliminar o repique
    espera_ms(30);

    // Nova leitura
    botao1 = PRESSIONADO(BOTAO1);
    botao2 = PRESSIONADO(BOTAO2);

    // Se o sinal desapareceu, descarta a leitura
    if (!botao1 && !botao2) {
      continue;
    }

    // Os dois juntos: aguarda soltar e ignora a tentativa
    if (botao1 && botao2) {
      aguardar_soltar();
      continue;
    }

    uint8_t resposta = botao1 ? 1 : 2;

    aguardar_soltar();

    return resposta;
  }
}

// 14. INDICAR ERRO (3 piscadas dos dois LEDs)

void indicar_erro() {
  for (uint8_t i = 0; i < 3; i++) {
    set_bit(PORTD, LED1);
    set_bit(PORTD, LED2);
    espera_ms(200);
    apagar_leds();
    espera_ms(200);
  }
}

// 15. INDICAR VITORIA (LEDs ficam acesos)

void indicar_vitoria() {
  set_bit(PORTD, LED1);
  set_bit(PORTD, LED2);
  espera_ms(1000);
  apagar_leds();
  espera_ms(300);
  set_bit(PORTD, LED1);
  set_bit(PORTD, LED2);
}

// 16. EXECUTAR O DESAFIO

void loop() {

  // Trata pedido de reinicio vindo da UART
  if (reiniciar_solicitado) {
    reiniciar_desafio();
  }

  // Desafio ja concluido: nao faz nada, loop livre para os outros desafios
  if (desafio_concluido) {
    return;
  }

  uint8_t acertou = 1;

  apresentar_sequencia();
  espera_ms(500);

  // Descarta qualquer aperto feito durante a apresentacao
  aguardar_soltar();

  for (uint8_t i = 0; i < TAMANHO; i++) {

    uint8_t resposta = ler_botao();

    // Reinicio pedido durante a espera: o proximo loop() trata
    if (resposta == 0) {
      return;
    }

    uart_enviar_linha_numero("Posicao: ", i + 1);
    uart_enviar_linha_numero("Esperado: ", sequencia[i]);
    uart_enviar_linha_numero("Recebido: ", resposta);

    if (resposta != sequencia[i]) {
      acertou = 0;
      break;
    }
  }

  if (acertou) {
    indicar_vitoria();
    desafio_concluido = true;
  } else {
    indicar_erro();
    espera_ms(1000);   // pausa antes de repetir a sequencia
  }
}