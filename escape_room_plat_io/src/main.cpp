// ========================================================================
// ESCAPE ROOM - DESARME A BOMBA (DAS 5332, UFSC)
// ========================================================================
//
// O QUE E
//   Jogo de escape room num Arduino Uno. O jogador escolhe a dificuldade,
//   o cronometro comeca em 3:00 e ele precisa resolver 4 desafios, um de
//   cada vez e em ordem sorteada, antes do tempo acabar. Cada erro desconta
//   5 s (facil) ou 10 s (dificil); no terceiro erro, ou com o tempo em zero,
//   a bomba "explode" e o jogo recomeca.
//
// DE ONDE VEIO CADA PARTE
//   bomba.ino                -> fluxo do jogo (dificuldade, cronometro, telas)
//   binario_decimal1 (2).ino -> desafio 1: converter hexadecimal em decimal
//   cofre.ino                -> desafio 2: 2 potenciometros nas faixas certas
//   desafio_sequencia.ino    -> desafio 3: repetir a sequencia dos LEDs
//   fios.ino                 -> desafio 4: ligar cada fio na cor certa
//   buzzer.ino               -> aviso sonoro
//
// COMO O PROGRAMA E ORGANIZADO (e por que)
//   1. Base de tempo unica: o Timer1 gera uma interrupcao a cada 1 ms.
//      A ISR so incrementa o contador "ms" e, a cada 1000 ms, desconta 1 s
//      do cronometro. Todo o resto do programa mede tempo comparando "ms",
//      em vez de usar delay(). Assim nada trava o jogo.
//   2. ISRs curtas: as 5 interrupcoes (Timer1, Timer2, INT0, PCINT0 e
//      recepcao da UART) so atualizam variaveis volatile ou levantam flags.
//      Quem decide o que fazer com isso e o loop() principal.
//   3. Laco principal sem bloqueio: o loop() le o teclado, atualiza o
//      cronometro e chama o desafio atual. Cada desafio faz um passo e
//      volta (maquina de estados), entao o tempo continua aparecendo e o
//      jogo pode explodir a qualquer momento.
//   4. Perifericos por registradores (exigencia do enunciado): GPIO (DDRx,
//      PORTx, PINx), Timer1, Timer2, ADC (ADMUX, ADCSRA), UART (UBRR0,
//      UCSR0x, UDR0) e interrupcoes externas (EICRA, EIMSK, PCICR, PCMSK0).
//      Bibliotecas so para o que a disciplina nao cobre: Wire (I2C) e
//      SSD1306Ascii (driver do OLED, github.com/greiman/SSD1306Ascii).
//
// MARCADORES NO CODIGO
//   [INTEGRACAO]   mudancas feitas para juntar os modulos num sketch so
//   [ALTERACAO ..] tempo no display, teclado 4x3, debounce, reinicio pelo *
//   [OLED]         troca do LCD 16x2 (PCF8574) pelo OLED 0,96" I2C
//   [TECLADO]      ordem real dos terminais e pull-up interno
//   [SORTEIO]      ordem dos desafios a cada partida
//   [FIOS]         circuito dos fios com LEDs e calibracao do ADC
//   [SEQUENCIA]    inicio da rodada pelo botao e LED no clique
//   [TESTE]        modo de teste de um desafio so (ambientes jogo_*)
//
// MAPA DE PINOS
//   D0/D1  UART 9600 8N1 ('R' reinicia o jogo; segurar '*' tambem reinicia)
//   D2     BOTAO1 da sequencia, interrupcao externa INT0
//   D3-D5  colunas do teclado 4x3 (1-4-7-*, 2-5-8-0, 3-6-9-#)
//   D6-D9  linhas do teclado 4x3 (pull-up interno)
//   D10    BOTAO2 da sequencia, interrupcao de mudanca de pino PCINT2
//   D11    fio superior amarelo + LED amarelo
//   D12    fio superior verde    + LED verde    (LED2 da sequencia / cofre 2)
//   D13    fio superior vermelho + LED vermelho (LED1 da sequencia / cofre 1)
//   A0     potenciometro do cofre 1
//   A1     potenciometro do cofre 2
//   A2     leitura dos fios inferiores (divisor de tensao)
//   A3     buzzer passivo
//   A4/A5  I2C: OLED SSD1306 128x64 (0x3C) e display HT16K33 (0x70)
// ========================================================================

#include <Arduino.h>
#include <Wire.h>
#include "SSD1306Ascii.h"     // [OLED] biblioteca do display OLED (greiman/SSD1306Ascii)
#include "SSD1306AsciiWire.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <util/delay.h>
#include <stdint.h>

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

// MACROS PARA OPERACOES DE BITS
#define set_bit(reg, bit) ((reg) |= (1 << (bit)))
#define clr_bit(reg, bit) ((reg) &= ~(1 << (bit)))
#define cpl_bit(reg, bit) ((reg) ^= (1 << (bit)))
#define tst_bit(reg, bit) ((reg) & (1 << (bit)))

// [INTEGRACAO] prototipos das funcoes usadas antes de serem definidas
void registrarErro();
void desligar_leds();
uint8_t gerar_numero_aleatorio();
void seq_aguardar_inicio();

// ========================================================================
// MODULO COMPARTILHADO: BASE DE TEMPO, UART E BUZZER
// ========================================================================
// COMO FUNCIONA
//   Recursos usados por todos os modulos:
//   - "ms": relogio de 1 ms incrementado pela ISR do Timer1. E lido com
//     ATOMIC_BLOCK porque tem 32 bits: o AVR le 1 byte por vez, e uma
//     interrupcao no meio da leitura daria um valor misturado.
//   - UART por registradores: 9600 baud, 8N1. A transmissao espera o
//     registrador UDR0 ficar livre (bit UDRE0). A recepcao e por
//     interrupcao e so levanta a flag de reinicio ao chegar 'R' ou 'r'.
//   - Buzzer: o Timer2 em modo CTC gera a frequencia, e a ISR inverte o
//     pino A3 a cada comparacao. O buzzer precisa ser passivo.

// ----------------------------------------
// BASE DE TEMPO DE 1 ms (Timer1)
// ----------------------------------------
// [INTEGRACAO] todos os modulos usavam o Timer1 com uma ISR propria
// (1 s na bomba, 1 ms nos fios/cofre/sequencia, 10 ms no hexadecimal).
// Agora existe uma unica ISR de 1 ms (ver modulo BOMBA), e este contador
// substitui "ms" (fios e sequencia) e "tempo_ms" (cofre).
volatile uint32_t ms = 0;

// LEITURA SEGURA DO TEMPO (fios.ino)
uint32_t ler_tempo_ms() {
  uint32_t tempo;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    tempo = ms;
  }

  return tempo;
}

// ----------------------------------------
// UART POR REGISTRADORES (desafio_sequencia.ino)
// ----------------------------------------
// [INTEGRACAO] substitui o Serial da bomba e dos fios e a UART do cofre.

#define BAUD 9600UL
#define UBRR_VALOR ((F_CPU / 16UL / BAUD) - 1)   // 103 para 9600 baud a 16 MHz

// Comando serial de reinicio
#define COMANDO_REINICIO 'R'

volatile bool reiniciar_solicitado = false;   // alterada na ISR da UART

// UART: recepcao de caractere. So sinaliza a flag de reinicio.
// [INTEGRACAO] agora reinicia o jogo inteiro, nao so a sequencia.
ISR(USART_RX_vect) {
  char c = UDR0;

  if (c == COMANDO_REINICIO || c == (COMANDO_REINICIO + 32)) {   // 'R' ou 'r'
    reiniciar_solicitado = true;
  }
}

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

// (versao de 16 bits do cofre.ino, serve tambem para a sequencia)
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

void uart_enviar_linha_numero(const char *rotulo, uint8_t valor) {
  uart_enviar_texto(rotulo);
  uart_enviar_numero(valor);
  uart_enviar_texto("\r\n");
}

// ----------------------------------------
// BUZZER (buzzer.ino)
// ----------------------------------------
// [INTEGRACAO] o original usava a saida OC2A (D11), que agora e o fio
// amarelo. O Timer2 continua gerando a frequencia em modo CTC, mas o pino
// do buzzer (A3) e invertido pela ISR do Timer2.

#define BUZZER_DDR  DDRC
#define BUZZER_PORT PORTC
#define BUZZER_BIT  PC3

void meu_tone(uint16_t frequencia) {
  // frequencia >= 245 Hz
  // f = 16 MHz / (2 * 128 * (OCR2A + 1))
  OCR2A = 62500 / frequencia - 1;
  // modo CTC, o pino e invertido na ISR a cada volta
  TCCR2A = (1 << WGM21);
  TCNT2 = 0;
  TIMSK2 = (1 << OCIE2A);
  TCCR2B = (1 << CS22) | (1 << CS20); // N = 128: liga
}

void meu_noTone() {
  TCCR2B = 0; // para o timer
  TIMSK2 = 0;
  TCCR2A = 0;
  clr_bit(BUZZER_PORT, BUZZER_BIT);
}

ISR(TIMER2_COMPA_vect) {
  cpl_bit(BUZZER_PORT, BUZZER_BIT);
}

// ========================================================================
// MODULO BOMBA (bomba.ino) - FLUXO DO JOGO
// ========================================================================

// ========================================
// ENDERECOS I2C
// ========================================

// [OLED] o LCD 16x2 com PCF8574 (0x20) foi trocado por um OLED 0,96" I2C.
// A maioria desses modulos responde em 0x3C (alguns em 0x3D).
#define OLED_ADDR 0x3C
#define DISPLAY_ADDR 0x70

// ========================================
// VARIAVEIS DO JOGO
// ========================================

volatile unsigned int tempoRestante = 180;
volatile byte atualizarTempo = 0;
volatile byte cronometroAtivo = 0;

byte jogoIniciado = 0;
byte jogoEncerrado = 0;
byte dificuldade = 0;

// [INTEGRACAO] regras de erro, penalidade e ordem dos desafios
#define TEMPO_TOTAL_S          180
#define PENALIDADE_FACIL_S     5
#define PENALIDADE_DIFICIL_S   10
#define MAX_ERROS_JOGO         3      // terceiro erro: a bomba explode
#define TEMPO_AVISO_S          30     // abaixo disso o buzzer apita a cada segundo
#define PAUSA_DESAFIOS_MS      2000UL // tempo da mensagem entre um desafio e outro
#define TEMPO_EXPLOSAO_MS      3000UL // tempo da tela de explosao antes de recomecar

#define NUM_DESAFIOS      4
#define DESAFIO_HEXADECIMAL   0
#define DESAFIO_COFRE     1
#define DESAFIO_SEQUENCIA 2
#define DESAFIO_FIOS      3

// [TESTE] modo de teste de um desafio so. O platformio.ini define
// DESAFIO_TESTE nos ambientes jogo_fios, jogo_cofre, jogo_sequencia e
// jogo_hexadecimal. No ambiente uno (jogo completo) ele nao existe.
#ifdef DESAFIO_TESTE
#define TOTAL_DESAFIOS 1
#else
#define TOTAL_DESAFIOS NUM_DESAFIOS
#endif

byte erros = 0;                     // erros somados de todos os desafios
byte ordemDesafios[NUM_DESAFIOS] = {
  DESAFIO_HEXADECIMAL, DESAFIO_COFRE, DESAFIO_SEQUENCIA, DESAFIO_FIOS
};
byte indiceDesafio = 0;             // posicao atual em ordemDesafios
byte desafioIniciado = 0;
byte vitoria = 0;
uint32_t esperarAte = 0;            // pausa entre desafios
uint32_t momentoFim = 0;
char teclaJogo = '\0';              // tecla lida neste loop, repassada ao hexadecimal

volatile uint16_t contadorSegundo = 0;

uint32_t fimBip = 0;
byte bipLigado = 0;

// ========================================
// TECLADO 4X3 (4 LINHAS E 3 COLUNAS)
// ========================================
// COMO FUNCIONA
//   Varredura de matriz: o teclado tem 12 teclas mas so 7 fios. Cada tecla
//   liga uma coluna a uma linha. O codigo coloca uma coluna por vez em 0V
//   e le as 4 linhas. As linhas tem pull-up interno, entao leem 1 quando
//   soltas e 0 quando a tecla que as liga a coluna em 0V esta apertada.
//   Sabendo qual coluna estava em 0V e qual linha caiu, sabe-se a tecla.
//   Debounce por tempo: a leitura so vale depois de ficar igual por 30 ms,
//   para ignorar o repique mecanico dos contatos.

// [ALTERACAO 4x3] o teclado 4x4 foi trocado por um 4x3.
// [TECLADO] ordem confirmada na bancada (teste 3): ligando os terminais
// da esquerda para a direita em D3..D9, os 3 primeiros sao as colunas e
// os 4 ultimos sao as linhas.
// Terminal 1 Coluna 1 (1 4 7 *) -> D3
// Terminal 2 Coluna 2 (2 5 8 0) -> D4
// Terminal 3 Coluna 3 (3 6 9 #) -> D5
// Terminal 4 Linha 1  (1 2 3)   -> D6
// Terminal 5 Linha 2  (4 5 6)   -> D7
// Terminal 6 Linha 3  (7 8 9)   -> D8
// Terminal 7 Linha 4  (* 0 #)   -> D9
//
// As colunas sao acionadas (saidas) e as linhas sao lidas (entradas com
// pull-up interno; resistores externos de 10k para 5V sao opcionais).

const char teclas[4][3] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};

// [TECLADO] mascara das colunas acionadas: D3 a D5 (D2 ficou para o BOTAO1)
#define COLUNAS_TECLADO 0b00111000

// [ALTERACAO DEBOUNCE] debounce por tempo, independente da velocidade do loop
#define INTERVALO_VARREDURA_MS 5    // uma varredura a cada 5 ms
#define DEBOUNCE_TECLADO_MS    30   // leitura igual por 30 ms para valer

char teclaPressionada = '\0';       // tecla estavel no momento ('\0' = nenhuma)
uint32_t inicioTeclaPressionada = 0; // quando ela foi aceita (para tecla segurada)

void iniciarTeclado() {

  // D3 a D5: saidas (colunas)
  DDRD |= COLUNAS_TECLADO;

  // D6 e D7: entradas (linhas 1 e 2)
  DDRD &= ~0b11000000;

  // D8 e D9: entradas (linhas 3 e 4)
  DDRB &= ~0b00000011;

  // Colunas em HIGH
  PORTD |= COLUNAS_TECLADO;

  // [TECLADO] pull-ups internos ligados nas linhas (D6 a D9).
  // Assim o teclado funciona com ou sem os resistores externos de 10k.
  PORTD |= 0b11000000;
  PORTB |= 0b00000011;
}

// [TECLADO] le uma linha: LOW quando a tecla da coluna acionada esta apertada
bool linhaPressionada(byte linha) {

  switch (linha) {

    case 0:
      return !(PIND & (1 << PD6));

    case 1:
      return !(PIND & (1 << PD7));

    case 2:
      return !(PINB & (1 << PB0));

    case 3:
      return !(PINB & (1 << PB1));
  }

  return false;
}

// [ALTERACAO DEBOUNCE] varre as 3 colunas e devolve a tecla apertada.
// Duas ou mais teclas ao mesmo tempo contam como nenhuma, para evitar
// leituras fantasmas.
char lerTecladoBruto() {

  char encontrada = '\0';
  byte quantidade = 0;

  for (byte coluna = 0; coluna < 3; coluna++) {

    // Todas as colunas em HIGH
    PORTD |= COLUNAS_TECLADO;

    // Somente uma coluna em LOW
    PORTD &= ~(1 << (coluna + 3));

    _delay_us(100);

    for (byte linha = 0; linha < 4; linha++) {

      if (linhaPressionada(linha)) {

        encontrada = teclas[linha][coluna];
        quantidade++;
      }
    }
  }

  PORTD |= COLUNAS_TECLADO;

  if (quantidade != 1) {
    return '\0';
  }

  return encontrada;
}

// Uma leitura por pressionamento.
// [ALTERACAO DEBOUNCE] a tecla so e aceita depois de ficar igual por
// DEBOUNCE_TECLADO_MS, e o mesmo vale para soltar. Antes eram 4 leituras
// seguidas, cujo tempo dependia da velocidade do loop.
char lerTeclado() {

  static char ultimaLeitura = '\0';
  static uint32_t inicioLeitura = 0;
  static uint32_t ultimaVarredura = 0;

  uint32_t agora = ler_tempo_ms();

  if (agora - ultimaVarredura < INTERVALO_VARREDURA_MS) {
    return '\0';
  }

  ultimaVarredura = agora;

  char atual = lerTecladoBruto();

  // Leitura mudou: recomeca a contagem do debounce
  if (atual != ultimaLeitura) {

    ultimaLeitura = atual;
    inicioLeitura = agora;

    return '\0';
  }

  // Ainda nao ficou estavel por tempo suficiente
  if (agora - inicioLeitura < DEBOUNCE_TECLADO_MS) {
    return '\0';
  }

  // Estavel, mas nada mudou em relacao a tecla aceita
  if (atual == teclaPressionada) {
    return '\0';
  }

  teclaPressionada = atual;
  inicioTeclaPressionada = agora;

  // Devolve a tecla ao apertar; ao soltar devolve '\0'
  return teclaPressionada;
}

// ========================================
// [OLED] DISPLAY OLED 0,96" I2C (SSD1306 128x64)
// ========================================
// COMO FUNCIONA
//   O jogo foi escrito para um LCD 16x2. Para trocar de display sem mexer
//   nas telas, as funcoes do LCD foram mantidas com o mesmo nome e agora
//   chamam a biblioteca SSD1306Ascii. Ela so desenha texto direto no OLED,
//   sem guardar uma copia da tela na RAM (o Uno so tem 2 KB).

// O resto do codigo continua usando as funcoes do LCD 16x2
// (limparLCD, linhaLCD, textoLCD, enviarLCD, comandoLCD). Aqui elas
// passam a desenhar no OLED. A fonte de 8 x 16 pixels da exatamente
// 16 colunas na tela de 128 pixels, igual ao LCD.
//   Linha 0 do LCD -> faixa de cima do OLED (paginas 0 e 1)
//   Linha 1 do LCD -> meio do OLED (paginas 4 e 5)

SSD1306AsciiWire oled;

#define OLED_LARGURA_CARACTERE 8   // 7 pixels da fonte + 1 de espaco
#define OLED_PAGINA_LINHA0     0
#define OLED_PAGINA_LINHA1     4

// Posiciona o cursor na coluna (0 a 15) e linha (0 ou 1) do "LCD"
void posicaoOLED(byte coluna, byte linha) {

  oled.setCursor(coluna * OLED_LARGURA_CARACTERE,
                 linha ? OLED_PAGINA_LINHA1 : OLED_PAGINA_LINHA0);
}

// Comandos do LCD que o jogo usa: 0x01 limpa a tela e 0x80 | endereco
// posiciona o cursor (0x00-0x0F linha 0, 0x40-0x4F linha 1).
void comandoLCD(byte valor) {

  if (valor == 0x01) {
    oled.clear();
  } else if (valor & 0x80) {
    byte endereco = valor & 0x7F;
    posicaoOLED(endereco & 0x0F, endereco >= 0x40);
  }
}

// rs = 1: escreve o caractere; rs = 0: e um comando
void enviarLCD(byte valor, byte rs) {

  if (rs) {
    oled.write(valor);
  } else {
    comandoLCD(valor);
  }
}

void textoLCD(const char* texto) {

  while (*texto) {

    enviarLCD(*texto, 1);
    texto++;
  }
}

void iniciarLCD() {

  oled.begin(&Adafruit128x64, OLED_ADDR);
  oled.setFont(ZevvPeep8x16);
  oled.setLetterSpacing(1);
  oled.clear();
}

void limparLCD() {
  comandoLCD(0x01);
}

void linhaLCD(byte linha) {

  if (linha == 0) {
    comandoLCD(0x80);
  } else {
    comandoLCD(0xC0);
  }
}

void telaInicial() {

  limparLCD();

  linhaLCD(0);
  textoLCD("ESCAPE ROOM");

  linhaLCD(1);
  textoLCD("PREPARE-SE");

  _delay_ms(1500);
}

void telaDificuldade() {

  limparLCD();

  linhaLCD(0);
  textoLCD("ESCOLHA O MODO");

  linhaLCD(1);
  textoLCD("1:FACIL 2:DIFIC");
}

void telaJogo() {

  limparLCD();

  linhaLCD(0);
  textoLCD("DESAFIOS ATIVOS");

  linhaLCD(1);

  // [ALTERACAO LCD] ate 11 caracteres: as colunas 12-15 sao do tempo
  if (dificuldade == 1) {
    textoLCD("MODO: FACIL");
  } else {
    textoLCD("MODO: DIFIC");
  }
}

void telaDerrota() {

  limparLCD();

  linhaLCD(0);
  textoLCD("TEMPO ESGOTADO");

  linhaLCD(1);
  textoLCD("FIM DE JOGO");
}

// [INTEGRACAO] novas telas
void telaExplosao() {

  limparLCD();

  linhaLCD(0);
  textoLCD("BOOM! 3 ERROS");

  linhaLCD(1);
  textoLCD("REINICIANDO...");
}

void telaVitoria() {

  limparLCD();

  linhaLCD(0);
  textoLCD("BOMBA DESARMADA");

  linhaLCD(1);
  textoLCD("SEGURE * REINIC.");   // [ALTERACAO] era "# PARA REINICIAR"
}

// Cabecalho de cada desafio: "1/4 NOME" e uma dica na linha de baixo
// [ALTERACAO LCD] a dica tem ate 11 caracteres: as colunas 12-15 sao do tempo
void telaDesafio(const char* nome, const char* dica) {

  limparLCD();

  linhaLCD(0);
  enviarLCD('1' + indiceDesafio, 1);
  enviarLCD('0' + TOTAL_DESAFIOS, 1);   // [TESTE] era sempre "/4"
  textoLCD(" ");
  textoLCD(nome);

  linhaLCD(1);
  textoLCD(dica);
}

// ========================================
// [ALTERACAO LCD] TEMPO RESTANTE NO LCD
// ========================================

// Durante o jogo o tempo fica no canto direito da linha de baixo
// (colunas 12 a 15, formato M:SS). Todas as telas do jogo deixam a
// coluna 11 em branco e escrevem no maximo 11 caracteres nessa linha.
#define COLUNA_TEMPO_LCD 12

// Pede para redesenhar o tempo no LCD (depois de uma tela nova)
byte tempoLCDPendente = 0;

void mostrarTempoLCD(unsigned int tempo) {

  comandoLCD(0xC0 + COLUNA_TEMPO_LCD);

  enviarLCD('0' + (tempo / 60), 1);
  enviarLCD(':', 1);
  enviarLCD('0' + ((tempo % 60) / 10), 1);
  enviarLCD('0' + (tempo % 10), 1);
}

// ========================================
// DISPLAY HT16K33
// ========================================
// COMO FUNCIONA
//   Display de 4 digitos de 7 segmentos com controlador HT16K33 no I2C.
//   Cada digito e um byte na memoria do HT16K33 (enderecos 0, 2, 6 e 8;
//   o 4 acende os dois pontos). "segmentos" diz quais segmentos acender
//   para cada numero de 0 a 9.

const byte segmentos[10] = {
  0x3F,
  0x06,
  0x5B,
  0x4F,
  0x66,
  0x6D,
  0x7D,
  0x07,
  0x7F,
  0x6F
};

void comandoDisplay(byte comando) {

  Wire.beginTransmission(DISPLAY_ADDR);
  Wire.write(comando);
  Wire.endTransmission();
}

void iniciarDisplay() {

  comandoDisplay(0x21);
  comandoDisplay(0x81);
  comandoDisplay(0xE8);
}

void mostrarTempo(byte minutos, byte segundos) {

  byte digitos[4] = {
    (byte)(minutos / 10),
    (byte)(minutos % 10),
    (byte)(segundos / 10),
    (byte)(segundos % 10)
  };

  byte memoria[16] = {0};

  memoria[0] = segmentos[digitos[0]];
  memoria[2] = segmentos[digitos[1]];

  // Dois pontos centrais
  memoria[4] = 0x02;

  memoria[6] = segmentos[digitos[2]];
  memoria[8] = segmentos[digitos[3]];

  Wire.beginTransmission(DISPLAY_ADDR);

  Wire.write((byte)0x00);

  for (byte i = 0; i < 16; i++) {
    Wire.write(memoria[i]);
  }

  Wire.endTransmission();
}

// ========================================
// TIMER1 - INTERRUPCAO A CADA 1 ms
// ========================================
// COMO FUNCIONA
//   Modo CTC (zera ao atingir OCR1A) com prescaler 64:
//   16 MHz / 64 = 250 kHz, ou seja 4 us por contagem. 250 contagens
//   (OCR1A = 249) dao 1 ms. A ISR conta "ms" e, a cada 1000 ms, desconta
//   1 s do cronometro e levanta a flag atualizarTempo para o loop()
//   redesenhar o tempo.

// [INTEGRACAO] era 1 s (prescaler 1024). Agora e 1 ms (prescaler 64),
// que e a base de tempo dos desafios. O segundo da bomba e contado na ISR.
void iniciarTimer1() {

  cli();

  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;

  // 16 MHz / 64 = 250 contagens por ms
  OCR1A = 249;

  // Modo CTC
  TCCR1B |= (1 << WGM12);

  // Prescaler 64
  TCCR1B |= (1 << CS11);
  TCCR1B |= (1 << CS10);

  // Habilitar interrupcao
  TIMSK1 |= (1 << OCIE1A);

  sei();
}

ISR(TIMER1_COMPA_vect) {

  ms++;

  contadorSegundo++;

  if (contadorSegundo < 1000) {
    return;
  }

  contadorSegundo = 0;

  if (cronometroAtivo && tempoRestante > 0) {

    tempoRestante--;

    atualizarTempo = 1;

    if (tempoRestante == 0) {
      cronometroAtivo = 0;
    }
  }
}

// ========================================
// [INTEGRACAO] BIP NAO BLOQUEANTE
// ========================================
// COMO FUNCIONA
//   bip() liga o tom e anota quando ele deve acabar. atualizarBip(),
//   chamada todo loop, desliga o tom na hora certa. Assim o som nao
//   trava o jogo como um delay() travaria.

void bip(uint16_t frequencia, uint16_t duracao) {

  meu_tone(frequencia);

  fimBip = ler_tempo_ms() + duracao;
  bipLigado = 1;
}

void atualizarBip() {

  if (bipLigado && (int32_t)(ler_tempo_ms() - fimBip) >= 0) {

    meu_noTone();
    bipLigado = 0;
  }
}

// ========================================
// [INTEGRACAO] ERROS E PENALIDADE
// ========================================
// COMO FUNCIONA
//   Todos os desafios chamam registrarErro(). Ela soma o erro e desconta a
//   penalidade com as interrupcoes desligadas (cli/sei), porque a ISR do
//   Timer1 tambem altera tempoRestante. servicoBomba() redesenha o tempo e
//   avisa o loop() quando a bomba deve explodir.

// Chamada pelos desafios a cada erro do jogador.
// Desconta 5 s (facil) ou 10 s (dificil) do cronometro.
void registrarErro() {

  byte penalidade = (dificuldade == 1) ? PENALIDADE_FACIL_S
                                       : PENALIDADE_DIFICIL_S;

  erros++;

  cli();

  if (tempoRestante > penalidade) {
    tempoRestante -= penalidade;
  } else {
    tempoRestante = 0;
  }

  atualizarTempo = 1;

  sei();

  bip(400, 300);

  uart_enviar_texto("ERRO ");
  uart_enviar_numero(erros);
  uart_enviar_texto(" DE 3 - PENALIDADE: ");
  uart_enviar_numero(penalidade);
  uart_enviar_linha(" s");
}

// Atualiza o display do cronometro e o aviso sonoro (era o meio do loop()
// da bomba). Devolve 0 se a bomba deve explodir (tempo esgotado ou 3 erros).
byte servicoBomba() {

  // Ler o cronometro com seguranca
  unsigned int tempoAtual;
  byte precisaAtualizar;

  cli();

  tempoAtual = tempoRestante;
  precisaAtualizar = atualizarTempo;
  atualizarTempo = 0;

  sei();

  if (precisaAtualizar) {

    mostrarTempo(
      tempoAtual / 60,
      tempoAtual % 60
    );

    // Aviso sonoro quando o tempo esta acabando
    if (tempoAtual > 0 && tempoAtual <= TEMPO_AVISO_S) {
      bip(1000, 100);
    }
  }

  // [ALTERACAO LCD] o tempo tambem aparece no LCD
  if (precisaAtualizar || tempoLCDPendente) {

    mostrarTempoLCD(tempoAtual);
    tempoLCDPendente = 0;
  }

  atualizarBip();

  return (tempoAtual > 0 && erros < MAX_ERROS_JOGO);
}

// ========================================================================
// DESAFIO 1: HEXADECIMAL -> DECIMAL (arquivo original binario_decimal1 (2).ino)
// ========================================================================
// COMO FUNCIONA
//   Sorteia um numero de 1 a 255 e mostra em hexadecimal (ex.: 0x2F).
//   O jogador digita o valor em decimal (2*16 + 15 = 47) e confirma com #.
//   O * (toque curto) apaga. Erro: confirmar um valor errado.
// [INTEGRACAO]
// - O I2C passa a ser feito pela biblioteca Wire (a mesma da bomba), em vez
//   do TWI por registradores, para nao misturar dois drivers no mesmo barramento.
// - O LCD ja e inicializado pela bomba (lcd_init removido).
// - O teclado proprio (D2-D5 / D8-D10) foi removido: o hexadecimal usa o teclado
//   da bomba (agora 4x3), cuja tecla e lida no loop principal (teclaJogo).
// - Os erros agora sao os erros do jogo (registrarErro), nao locais.

// [OLED] o hexadecimal escrevia direto no PCF8574 do LCD. Agora cada byte vai
// para enviarLCD() da bomba, que desenha no OLED.
#define LCD_RS            0x01   // indica caractere (e nao comando)

#define FASE_BITS         8      // número sorteado de 8 bits (0x00 a 0xFF)
#define FASE_NUM_MIN      1      // menor número sorteado
#define FASE_MAX_DIGITOS  3      // máximo de dígitos decimais digitados

// estados do desafio
#define DESAFIO_ANDAMENTO 0
#define DESAFIO_CONCLUIDO 1

// resultados devolvidos por fase_teclado_atualiza()
#define FASE_EM_ANDAMENTO 0
#define FASE_ACERTOU      1
#define FASE_ERROU        2

static uint8_t estado_desafio = DESAFIO_ANDAMENTO;

static void lcd_byte(uint8_t valor, uint8_t rs) {
  enviarLCD(valor, rs ? 1 : 0);
}

// envia um comando ao LCD. limpar e voltar ao início levam cerca de 2 ms
static void lcd_comando(uint8_t comando) {
  lcd_byte(comando, 0);
}

// escreve um caractere na posição atual do cursor
static void lcd_caractere(char c) {
  lcd_byte(c, LCD_RS);
}

// escreve um texto a partir da posição atual do cursor
static void lcd_texto(const char *texto) {
  while (*texto) lcd_caractere(*texto++);
}

// escreve um número decimal sem sinal (até 5 dígitos)
static void lcd_numero(uint16_t numero) {
  char invertido[5];
  uint8_t n = 0;
  if (numero == 0) {
    lcd_caractere('0');
    return;
  }
  while (numero > 0) {
    invertido[n++] = '0' + (numero % 10);
    numero /= 10;
  }
  while (n > 0) lcd_caractere(invertido[--n]);
}

// move o cursor para a coluna (0 a 15) e a linha (0 ou 1)
static void lcd_posicao(uint8_t coluna, uint8_t linha) {
  lcd_comando(0x80 | (coluna + (linha ? 0x40 : 0x00)));
}

//escreve um texto na linha indicada, completando com espaços até 16 colunas
static void lcd_linha(uint8_t linha, const char *texto) {
  lcd_posicao(0, linha);
  uint8_t n = 0;
  while (texto[n] && n < 16) {
    lcd_caractere(texto[n]);
    n++;
  }
  while (n < 16) {
    lcd_caractere(' ');
    n++;
  }
}

//mostra na linha 1 o número que o jogador deve converter (ex.: "Hex:0x2F")
void display_mostra_enunciado(const char *texto) {
  lcd_posicao(0, 0);
  lcd_texto("Hex:");
  lcd_texto(texto);
  for (uint8_t i = 8; i < 16; i++) lcd_caractere(' ');
}

// mostra na linha 2 o valor decimal digitado até agora (ex.: "Dec:47")
// completa o campo com espaços, o que tambem apaga a mensagem "Errou!"
// [ALTERACAO LCD] o "Errou!" agora fica na linha 2, no lugar do "Dec:",
// e a linha 1 nao e mais limpa aqui (guarda os erros)
void display_mostra_digitado(uint16_t valor, uint8_t qtd_digitos) {
  uint8_t n = 0;
  if (qtd_digitos > 0) n = (valor < 10) ? 1 : (valor < 100) ? 2 : 3;
  lcd_posicao(0, 1);
  lcd_texto("Dec:");
  if (qtd_digitos > 0) lcd_numero(valor);
  for (uint8_t i = 4 + n; i < 7; i++) lcd_caractere(' ');
}

// mostra a quantidade de erros no canto direito da linha 1 (ex.: "Erros:1")
// [INTEGRACAO] mostra os erros do jogo inteiro
// [ALTERACAO LCD] saiu da linha 2 (colunas 7-13) para a linha 1 (colunas
// 9-15), porque o tempo ocupa o fim da linha 2
static void mostra_erros(void) {
  lcd_posicao(9, 0);
  lcd_texto("Erros:");
  lcd_numero(erros);
}

// [INTEGRACAO] a tecla vem do teclado da bomba (lida uma vez por loop)
static char teclado_tecla(void) {
  return teclaJogo;
}

static uint8_t  numero_alvo = 0;
static uint16_t valor_digitado = 0;
static uint8_t  qtd_digitos = 0;
static uint16_t estado_aleatorio = 1;


static uint16_t proximo_aleatorio(void) {
  estado_aleatorio ^= estado_aleatorio << 7;
  estado_aleatorio ^= estado_aleatorio >> 9;
  estado_aleatorio ^= estado_aleatorio << 8;
  return estado_aleatorio;
}

// Converte um número em texto hexadecimal

void hex_para_texto(uint8_t numero, uint8_t bits, char *saida) {
  uint8_t k = 0;
  saida[k++] = '0';
  saida[k++] = 'x';
  for (int8_t i = ((bits + 3) / 4) - 1; i >= 0; i--) {
    uint8_t nibble = (numero >> (4 * i)) & 0x0F;
    saida[k++] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
  }
  saida[k] = '\0';
}

// sorteia um número, mostra em hexadecimal e zera o que foi digitado
void fase_teclado_inicia(uint16_t semente) {
  uint8_t maximo = (uint8_t)((1u << FASE_BITS) - 1);
  char texto[5];

  estado_aleatorio = semente | 1;
  numero_alvo = FASE_NUM_MIN + (proximo_aleatorio() % (uint16_t)(maximo - FASE_NUM_MIN + 1));
  valor_digitado = 0;
  qtd_digitos = 0;

  hex_para_texto(numero_alvo, FASE_BITS, texto);
  display_mostra_enunciado(texto);
  display_mostra_digitado(0, 0);
  mostra_erros();
}

// verifica a tecla apertada (se houver): dígitos montam o valor decimal,
// '*' apaga e '#' confirma. Devolve ACERTOU ou ERROU uma única vez a cada
// confirmação, e EM_ANDAMENTO nos demais casos
uint8_t fase_teclado_atualiza(void) {
  char tecla = teclado_tecla();
  if (!tecla) return FASE_EM_ANDAMENTO;

  if (tecla >= '0' && tecla <= '9') {
    if (qtd_digitos < FASE_MAX_DIGITOS) {
      valor_digitado = valor_digitado * 10 + (tecla - '0');
      qtd_digitos++;
      display_mostra_digitado(valor_digitado, qtd_digitos);
    }
  } else if (tecla == '*') {
    valor_digitado = 0;
    qtd_digitos = 0;
    display_mostra_digitado(0, 0);
  } else if (tecla == '#') {
    if (qtd_digitos == 0) return FASE_EM_ANDAMENTO;
    uint8_t acertou = (valor_digitado == numero_alvo);
    valor_digitado = 0;
    qtd_digitos = 0;
    display_mostra_digitado(0, 0);
    if (acertou) return FASE_ACERTOU;
    registrarErro();   // [INTEGRACAO] penalidade e contagem do jogo
    mostra_erros();
    return FASE_ERROU;
  }
  return FASE_EM_ANDAMENTO;
}

// [INTEGRACAO] era o setup(): sorteia o numero e desenha a tela
void hexadecimal_iniciar() {
  estado_desafio = DESAFIO_ANDAMENTO;
  fase_teclado_inicia((uint16_t)ler_tempo_ms() ^ TCNT1);
}

// [INTEGRACAO] era o loop(): devolve 1 quando o desafio foi concluido.
// O terceiro erro agora e tratado pela bomba (explosao).
byte hexadecimal_executar() {
  if (estado_desafio != DESAFIO_ANDAMENTO) return 1;

  uint8_t r = fase_teclado_atualiza();
  if (r == FASE_ACERTOU) {
    estado_desafio = DESAFIO_CONCLUIDO;
    lcd_linha(0, "Parabens, passou");
    lcd_linha(1, "de fase!");   // [ALTERACAO LCD] era "p/ proxima fase"
    return 1;
  } else if (r == FASE_ERROU) {
    lcd_posicao(0, 1);          // [ALTERACAO LCD] era a coluna 9 da linha 1
    lcd_texto("Errou!");
  }
  return 0;
}

// ========================================================================
// DESAFIO 2: COFRE (cofre.ino)
// ========================================================================
// COMO FUNCIONA
//   O ADC le os dois potenciometros alternando os canais A0 e A1, sem
//   esperar a conversao: se ela terminou, guarda o valor e ja dispara a
//   proxima. Faz media de 8 leituras para tirar ruido. Cada LED acende
//   quando o seu potenciometro esta na faixa certa, com histerese para nao
//   piscar na borda. O cofre abre com os dois na faixa por 1 s seguido.
//   Nao tem erro.
// [INTEGRACAO]
// - LEDs mudaram de D5/D6 (agora teclado) para D13/D12.
// - Timer1 e UART vem do modulo compartilhado.
// - setup() virou cofre_iniciar() e loop() virou cofre_executar().

// 2. DEFINICAO DOS PINOS

// LEDs indicadores de cada cofre (compartilhados com a sequencia e os fios)
#define LED_COFRE_DDR  DDRB
#define LED_COFRE_PORT PORTB
#define LED_COFRE1 PB5   // D13
#define LED_COFRE2 PB4   // D12

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

// 4. VARIAVEIS

uint16_t leitura_A0 = 0;
uint16_t leitura_A1 = 0;

uint16_t soma_A0 = 0;
uint16_t soma_A1 = 0;
uint8_t amostras_A0 = 0;
uint8_t amostras_A1 = 0;

uint8_t canal = 0;
uint8_t acender_led1 = 0;
uint8_t acender_led2 = 0;

bool cofre_concluido = false;   // vira true quando o cofre abre
bool contando_estavel = false;
uint16_t inicio_estavel = 0;
uint16_t ultimo_log = 0;

// 6. TEMPO (SEM BLOQUEIO)

// [INTEGRACAO] usa o contador de 1 ms compartilhado
uint16_t agora_ms() {
  return (uint16_t)ler_tempo_ms();
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
    set_bit(LED_COFRE_PORT, LED_COFRE1);
  } else {
    clr_bit(LED_COFRE_PORT, LED_COFRE1);
  }
}

void atualiza_led2(uint16_t valor) {
  acender_led2 = calcula_estado(valor, LIMIAR_BAIXO2, LIMIAR_ALTO2, acender_led2);

  if (acender_led2) {
    set_bit(LED_COFRE_PORT, LED_COFRE2);
  } else {
    clr_bit(LED_COFRE_PORT, LED_COFRE2);
  }
}

// 9. CONFIGURACAO INICIAL

void cofre_iniciar() {
  // [INTEGRACAO] zera o estado para cada nova partida
  leitura_A0 = 0;
  leitura_A1 = 0;
  soma_A0 = 0;
  soma_A1 = 0;
  amostras_A0 = 0;
  amostras_A1 = 0;
  canal = 0;
  acender_led1 = 0;
  acender_led2 = 0;
  cofre_concluido = false;
  contando_estavel = false;

  // LEDs como saidas, inicialmente apagados
  set_bit(LED_COFRE_DDR, LED_COFRE1);
  set_bit(LED_COFRE_DDR, LED_COFRE2);
  clr_bit(LED_COFRE_PORT, LED_COFRE1);
  clr_bit(LED_COFRE_PORT, LED_COFRE2);

  ADMUX = (1 << REFS0); // Referencia 5V, canal 0
                        // Clock: 16 / 128
  ADCSRA = (1 << ADEN)
         | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
  set_bit(ADCSRA, ADSC); // Primeira conversao
}

// 10. EXECUTAR O DESAFIO
// [INTEGRACAO] devolve 1 quando o cofre abre

byte cofre_executar() {

  // Cofre ja aberto
  if (cofre_concluido) {
    return 1;
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
      cofre_concluido = true;   // LEDs permanecem acesos
      uart_enviar_linha("COFRE ABERTO");
      return 1;
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

  return 0;
}

// ========================================================================
// DESAFIO 3: SEQUENCIA (desafio_sequencia.ino)
// ========================================================================
// COMO FUNCIONA
//   Maquina de estados: cada chamada de sequencia_executar() faz um passo
//   e volta, sem travar o jogo.
//   AGUARDA_INICIO ("PRESSIONE BOTAO") -> APRESENTANDO (os 2 LEDs piscam e
//   a sequencia 1,2,2,1,2 e mostrada) -> PAUSA -> ESPERA_BOTAO ->
//   CONFIRMA_BOTAO (debounce) -> SOLTAR -> SOLTAR_REPIQUE -> confere.
//   Os botoes geram interrupcao (INT0 no D2, PCINT2 no D10), e a ISR so
//   anota qual botao foi. Erro: apertar o botao errado.
// [INTEGRACAO]
// - LEDs mudaram de D6/D4 (agora teclado) para D13/D12.
// - BOTAO1 continua em D2 e agora gera a interrupcao externa INT0.
//   BOTAO2 mudou de D3 (agora teclado) para D10 e gera a interrupcao de
//   mudanca de pino PCINT2 (grupo PCINT0). As ISRs so levantam uma flag.
// - O desafio virou uma maquina de estados sem bloqueio: as antigas
//   funcoes com espera (apresentar_sequencia, ler_botao, aguardar_soltar,
//   indicar_erro, indicar_vitoria, espera_ms) viraram estados.
// - Timer1 e UART vem do modulo compartilhado; o 'R' reinicia o jogo todo.
// - Cada erro chama registrarErro().

// 2. DEFINICAO DOS PINOS

// LEDs (compartilhados com o cofre e os fios)
#define LED_DDR  DDRB
#define LED_PORT PORTB
#define LED1 PB5   // D13
#define LED2 PB4   // D12

// Botoes
#define BOTAO1      PD2   // D2 (INT0)
#define BOTAO1_DDR  DDRD
#define BOTAO1_PORT PORTD
#define BOTAO1_PIN  PIND

#define BOTAO2      PB2   // D10 (PCINT2)
#define BOTAO2_DDR  DDRB
#define BOTAO2_PORT PORTB
#define BOTAO2_PIN  PINB

// Tipo de ligacao dos botoes:
// 1 = botao liga o pino ao 5V, com resistor pull-down externo (le 1 ao pressionar)
// 0 = botao liga o pino ao GND, usa pull-up interno (le 0 ao pressionar)
#define PULLDOWN_EXTERNO 1

// [INTEGRACAO] cada botao le o registrador PIN da sua porta
#if PULLDOWN_EXTERNO
  #define PRESSIONADO(pino) (tst_bit(pino##_PIN, pino) != 0)
#else
  #define PRESSIONADO(pino) (tst_bit(pino##_PIN, pino) == 0)
#endif

// Tempos (ms), os mesmos do codigo original
#define SEQ_TEMPO_LED       300   // LED aceso / apagado na apresentacao
#define SEQ_PAUSA_RESPOSTA  500   // pausa entre a apresentacao e a resposta
#define SEQ_DEBOUNCE        30    // repique dos botoes
#define SEQ_PISCA_ERRO      200   // piscadas de erro
#define SEQ_PAUSA_ERRO      1000  // pausa antes de repetir a sequencia
#define SEQ_VITORIA_ACESO   1000
#define SEQ_VITORIA_APAGADO 300
#define SEQ_PISCA_CLIQUE    150   // [SEQUENCIA] LED do botao aceso no clique

// Estados da maquina
#define SEQ_APRESENTANDO   0   // era apresentar_sequencia()
#define SEQ_PAUSA          1   // era espera_ms(500)
#define SEQ_ESPERA_BOTAO   2   // era ler_botao(): espera a flag da interrupcao
#define SEQ_CONFIRMA_BOTAO 3   // era ler_botao(): debounce e nova leitura
#define SEQ_SOLTAR         4   // era aguardar_soltar(): espera soltar
#define SEQ_SOLTAR_REPIQUE 5   // era aguardar_soltar(): repique na liberacao
#define SEQ_ERRO           6   // era indicar_erro() + espera_ms(1000)
#define SEQ_VITORIA        7   // era indicar_vitoria()
#define SEQ_AGUARDA_INICIO 8   // [SEQUENCIA] "PRESSIONE BOTAO" antes da rodada
#define SEQ_INICIO_SOLTAR  9   // [SEQUENCIA] espera soltar o botao do inicio

// 3. VARIAVEIS

bool sequencia_concluida = false;

// Sequencia:
// 1 = LED1 / BOTAO1
// 2 = LED2 / BOTAO2
const uint8_t sequencia[] = {1, 2, 2, 1, 2};
const uint8_t TAMANHO = sizeof(sequencia);

// Botao que gerou a interrupcao: 0 = nenhum, 1 = BOTAO1, 2 = BOTAO2
volatile uint8_t botao_evento = 0;

uint8_t seq_estado = SEQ_APRESENTANDO;
uint8_t seq_passo = 0;        // passo dentro do estado (piscadas, LEDs)
uint8_t seq_posicao = 0;      // posicao da resposta na sequencia
uint8_t seq_resposta = 0;     // botao lido (0 = descartar)
uint32_t seq_marca = 0;       // instante em que o passo atual comecou
uint32_t seq_marca_led = 0;   // [SEQUENCIA] quando o LED do clique acendeu

// 4. INTERRUPCOES DOS BOTOES (so levantam a flag)

// BOTAO1: INT0 (D2)
ISR(INT0_vect) {
  botao_evento = 1;
}

// BOTAO2: PCINT2 (D10). Dispara nas duas bordas, so conta o aperto.
ISR(PCINT0_vect) {
  if (PRESSIONADO(BOTAO2)) {
    botao_evento = 2;
  }
}

// 5. CONTROLE DE TEMPO DA MAQUINA DE ESTADOS

void seq_ir_para(uint8_t estado) {
  seq_estado = estado;
  seq_passo = 0;
  seq_marca = ler_tempo_ms();
}

uint32_t seq_decorrido() {
  return ler_tempo_ms() - seq_marca;
}

// Avanca um passo e reinicia a contagem de tempo do passo
void seq_proximo_passo() {
  seq_passo++;
  seq_marca = ler_tempo_ms();
}

// 7. CONFIGURACAO INICIAL

void sequencia_iniciar() {
  // LEDs como saidas
  set_bit(LED_DDR, LED1);
  set_bit(LED_DDR, LED2);

  // Botoes como entradas
  clr_bit(BOTAO1_DDR, BOTAO1);
  clr_bit(BOTAO2_DDR, BOTAO2);

#if PULLDOWN_EXTERNO
  // Pull-up interno desligado (usa pull-down externo)
  clr_bit(BOTAO1_PORT, BOTAO1);
  clr_bit(BOTAO2_PORT, BOTAO2);

  // INT0 na borda de subida (botao pressionado)
  EICRA = (EICRA & ~((1 << ISC01) | (1 << ISC00))) | (1 << ISC01) | (1 << ISC00);
#else
  // Pull-up interno ligado (botao vai ao GND)
  set_bit(BOTAO1_PORT, BOTAO1);
  set_bit(BOTAO2_PORT, BOTAO2);

  // INT0 na borda de descida (botao pressionado)
  EICRA = (EICRA & ~((1 << ISC01) | (1 << ISC00))) | (1 << ISC01);
#endif

  // Limpa pedidos pendentes e habilita INT0 e PCINT2
  EIFR = (1 << INTF0);
  PCIFR = (1 << PCIF0);
  set_bit(EIMSK, INT0);
  set_bit(PCMSK0, PCINT2);
  set_bit(PCICR, PCIE0);

  // LEDs inicialmente apagados
  clr_bit(LED_PORT, LED1);
  clr_bit(LED_PORT, LED2);

  botao_evento = 0;
  seq_posicao = 0;
  sequencia_concluida = false;

  seq_aguardar_inicio();   // [SEQUENCIA] era seq_ir_para(SEQ_APRESENTANDO)
}

// 8. APAGAR OS DOIS LEDS

void apagar_leds() {
  clr_bit(LED_PORT, LED1);
  clr_bit(LED_PORT, LED2);
}

// 9. ACENDER O LED CORRESPONDENTE

void acender_led(uint8_t valor) {
  if (valor == 1) {
    set_bit(LED_PORT, LED1);
  } else if (valor == 2) {
    set_bit(LED_PORT, LED2);
  }
}

// 12. REINICIAR O DESAFIO

void reiniciar_desafio() {
  apagar_leds();
  sequencia_concluida = false;
  reiniciar_solicitado = false;

  uart_enviar_linha("Desafio reiniciado");
}

// 13. CONFERIR A RESPOSTA DE UM BOTAO

// [SEQUENCIA] cada rodada so comeca quando o jogador aperta um botao
void seq_aguardar_inicio() {

  apagar_leds();
  botao_evento = 0;

  limparLCD();
  linhaLCD(0);
  textoLCD("PRESSIONE BOTAO");
  linhaLCD(1);
  textoLCD("P/ COMECAR");
  tempoLCDPendente = 1;   // redesenha o tempo no canto

  seq_ir_para(SEQ_AGUARDA_INICIO);
}

void seq_conferir_resposta() {

  uart_enviar_linha_numero("Posicao: ", seq_posicao + 1);
  uart_enviar_linha_numero("Esperado: ", sequencia[seq_posicao]);
  uart_enviar_linha_numero("Recebido: ", seq_resposta);

  if (seq_resposta != sequencia[seq_posicao]) {
    registrarErro();   // [INTEGRACAO]
    seq_ir_para(SEQ_ERRO);
    return;
  }

  seq_posicao++;

  if (seq_posicao >= TAMANHO) {
    seq_ir_para(SEQ_VITORIA);
  } else {
    seq_ir_para(SEQ_ESPERA_BOTAO);
  }
}

// 16. EXECUTAR O DESAFIO
// [INTEGRACAO] executa um passo da maquina de estados e volta.
// Devolve 1 quando o desafio foi concluido.

byte sequencia_executar() {

  // Desafio ja concluido
  if (sequencia_concluida) {
    return 1;
  }

  switch (seq_estado) {

    // Pisca os dois LEDs juntos e depois a sequencia, um passo a cada
    // SEQ_TEMPO_LED. Passo par acende, passo impar apaga.
    case SEQ_APRESENTANDO:
      if (seq_passo != 0 && seq_decorrido() < SEQ_TEMPO_LED) {
        break;
      }

      if (seq_passo >= 2 * (TAMANHO + 1)) {
        seq_ir_para(SEQ_PAUSA);
        break;
      }

      if (seq_passo % 2 == 0) {
        uint8_t item = seq_passo / 2;

        if (item == 0) {
          // Acende os dois LEDs simultaneamente
          acender_led(1);
          acender_led(2);
        } else {
          acender_led(sequencia[item - 1]);
        }
      } else {
        apagar_leds();
      }

      seq_proximo_passo();
      break;

    // Pausa e descarta qualquer aperto feito durante a apresentacao
    case SEQ_PAUSA:
      if (seq_decorrido() >= SEQ_PAUSA_RESPOSTA) {
        seq_posicao = 0;
        seq_resposta = 0;
        seq_ir_para(SEQ_SOLTAR);
      }
      break;

    // Espera a flag levantada por INT0 ou PCINT2
    case SEQ_ESPERA_BOTAO:
      if (botao_evento) {
        botao_evento = 0;
        seq_ir_para(SEQ_CONFIRMA_BOTAO);
      }
      break;

    // Depois do repique, le os dois botoes de novo
    case SEQ_CONFIRMA_BOTAO:
      if (seq_decorrido() >= SEQ_DEBOUNCE) {
        uint8_t botao1 = PRESSIONADO(BOTAO1);
        uint8_t botao2 = PRESSIONADO(BOTAO2);

        if (!botao1 && !botao2) {
          // Se o sinal desapareceu, descarta a leitura
          seq_ir_para(SEQ_ESPERA_BOTAO);
        } else {
          // Os dois juntos: ignora a tentativa (resposta 0)
          seq_resposta = (botao1 && botao2) ? 0 : (botao1 ? 1 : 2);

          // [SEQUENCIA] acende o LED do botao apertado
          if (seq_resposta != 0) {
            acender_led(seq_resposta);
            seq_marca_led = ler_tempo_ms();
          }

          seq_ir_para(SEQ_SOLTAR);
        }
      }
      break;

    // Espera os dois botoes serem soltos
    case SEQ_SOLTAR:
      if (!PRESSIONADO(BOTAO1) && !PRESSIONADO(BOTAO2)) {
        seq_ir_para(SEQ_SOLTAR_REPIQUE);
      }
      break;

    // Elimina o repique na liberacao e entao confere a resposta
    case SEQ_SOLTAR_REPIQUE:
      if (PRESSIONADO(BOTAO1) || PRESSIONADO(BOTAO2)) {
        seq_ir_para(SEQ_SOLTAR);
      } else if (seq_decorrido() >= SEQ_DEBOUNCE &&
                 ler_tempo_ms() - seq_marca_led >= SEQ_PISCA_CLIQUE) {
        // Ignora as interrupcoes geradas pelo repique
        botao_evento = 0;

        // [SEQUENCIA] apaga o LED do clique (fica aceso no minimo 150 ms)
        apagar_leds();

        if (seq_resposta == 0) {
          seq_ir_para(SEQ_ESPERA_BOTAO);
        } else {
          seq_conferir_resposta();
        }
      }
      break;

    // [SEQUENCIA] espera qualquer botao para comecar a rodada
    case SEQ_AGUARDA_INICIO:
      if (botao_evento) {
        botao_evento = 0;
        seq_ir_para(SEQ_INICIO_SOLTAR);
      }
      break;

    // [SEQUENCIA] botao solto por SEQ_DEBOUNCE: apresenta a sequencia
    case SEQ_INICIO_SOLTAR:
      if (PRESSIONADO(BOTAO1) || PRESSIONADO(BOTAO2)) {
        seq_marca = ler_tempo_ms();
      } else if (seq_decorrido() >= SEQ_DEBOUNCE) {
        botao_evento = 0;
        telaDesafio("SEQUENCIA", "REPITA LEDS");
        tempoLCDPendente = 1;
        uart_enviar_linha("RODADA DA SEQUENCIA");
        seq_ir_para(SEQ_APRESENTANDO);
      }
      break;

    // 3 piscadas dos dois LEDs e pausa antes de repetir a sequencia
    case SEQ_ERRO:
      if (seq_passo < 6) {
        if (seq_passo == 0 || seq_decorrido() >= SEQ_PISCA_ERRO) {
          if (seq_passo % 2 == 0) {
            acender_led(1);
            acender_led(2);
          } else {
            apagar_leds();
          }
          seq_proximo_passo();
        }
      } else if (seq_decorrido() >= SEQ_PISCA_ERRO + SEQ_PAUSA_ERRO) {
        seq_aguardar_inicio();   // [SEQUENCIA] nova rodada pelo botao
      }
      break;

    // LEDs acesos, apagados e acesos de novo (ficam acesos)
    case SEQ_VITORIA:
      if (seq_passo == 0) {
        acender_led(1);
        acender_led(2);
        seq_proximo_passo();
      } else if (seq_passo == 1 && seq_decorrido() >= SEQ_VITORIA_ACESO) {
        apagar_leds();
        seq_proximo_passo();
      } else if (seq_passo == 2 && seq_decorrido() >= SEQ_VITORIA_APAGADO) {
        acender_led(1);
        acender_led(2);
        sequencia_concluida = true;
        return 1;
      }
      break;
  }

  return 0;
}

// ========================================================================
// DESAFIO 4: FIOS (fios.ino)
// ========================================================================
// COMO FUNCIONA
//   Cada fio de cima sai de um pino (D13, D12, D11). Cada terminal de
//   baixo tem um LED e um resistor diferente e todos terminam num ponto
//   comum, ligado ao A2 e a um resistor de 1k para o GND (divisor de
//   tensao). O codigo coloca um fio de cima por vez em 5V, deixa os outros
//   em alta impedancia e le o A2: a tensao diz em qual terminal de baixo
//   esse fio esta. Os LEDs de baixo tambem funcionam como diodos e impedem
//   a corrente de voltar por outro fio.
//   Vitoria: os 3 fios nas cores certas por 0,5 s.
//   Erro: fio vermelho de cima no terminal amarelo (conta uma vez).
// [INTEGRACAO]
// - Leitura dos fios inferiores mudou de A1 (agora cofre) para A2.
// - O display de 7 segmentos e do cronometro da bomba; o contador de 5 s
//   dos fios saiu e o numero sorteado aparece no LCD.
// - A ligacao proibida (vermelho em amarelo) chama registrarErro() em vez
//   de somar 5 s em penalidade_ms.
// - O desafio termina quando os 3 fios estao na cor certa por 500 ms
//   (antes era conferido uma unica vez depois de 5 s, com while(1) no fim).

// Tempo que os 3 fios precisam ficar corretos para concluir
#define FIOS_TEMPO_ESTAVEL_MS 500UL

bool fios_concluido = false;

// SEMENTE ALEATORIA
uint32_t semente_aleatoria = 123456789UL;

// FIOS SUPERIORES
// 0 = vermelho (D13)
// 1 = verde (D12)
// 2 = amarelo (D11)

const uint8_t fios_cima[3] = {
  PB5, PB4, PB3
};

// [TESTE] ultimo valor do ADC (A2) lido para cada fio superior
uint16_t fios_ultimo_valor[3] = {0, 0, 0};

// CONTROLE DAS CONEXOES
uint8_t candidato[3] = {255, 255, 255};
uint32_t inicio_contato[3] = {0};

bool registrado[3][3] = {};

// CONFIGURACAO DOS PINOS
void configurar_pinos() {
  set_bit(DDRB, PB3);
  set_bit(DDRB, PB4);
  set_bit(DDRB, PB5);

  clr_bit(PORTB, PB3);
  clr_bit(PORTB, PB4);
  clr_bit(PORTB, PB5);

  // [INTEGRACAO] A2 (era A1)
  clr_bit(DDRC, PC2);
  clr_bit(PORTC, PC2);
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

// CONFIGURACAO DO ADC
void configurar_adc() {
  // [INTEGRACAO] canal A2 (MUX1), era A1 (MUX0)
  ADMUX = (1 << REFS0) | (1 << MUX1);

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

// IDENTIFICA O FIO INFERIOR
// 0 = vermelho
// 1 = verde
// 2 = amarelo
// 255 = nenhum

// [FIOS] faixas do ADC de cada cor, para o circuito com LED em baixo:
//   fio de cima (5V) -> LED de baixo -> resistor -> A2 -> 1k -> GND
// Calibrado na bancada (jogo_fios), com os 3 fios ligados nas cores certas:
//   vermelho ~143 | verde ~559 | amarelo ~55
// Cada divisa fica no meio do caminho entre duas leituras.
// Valores antigos: vermelho 200-230, verde 95-125, amarelo 170-195.
#define FIO_VERMELHO_MIN 100
#define FIO_VERMELHO_MAX 349
#define FIO_VERDE_MIN    350
#define FIO_VERDE_MAX    1023
#define FIO_AMARELO_MIN  25
#define FIO_AMARELO_MAX  99

uint8_t identificar_fio(uint16_t valor) {

  // VERMELHO
  if (valor >= FIO_VERMELHO_MIN && valor <= FIO_VERMELHO_MAX) {
    return 0;
  }

  // VERDE
  if (valor >= FIO_VERDE_MIN && valor <= FIO_VERDE_MAX) {
    return 1;
  }

  // AMARELO
  if (valor >= FIO_AMARELO_MIN && valor <= FIO_AMARELO_MAX) {
    return 2;
  }

  return 255;
}

// TESTA UM FIO SUPERIOR
// [FIOS] os outros dois fios ficam em alta impedancia (entrada, sem
// pull-up) durante a medida. Antes eles ficavam em 0V e, se estivessem
// ligados, puxavam a tensao do A2 para baixo e mudavam a leitura.
uint8_t testar_fio(uint8_t cima) {

  // Os 3 fios como entradas, sem pull-up
  DDRB &= ~((1 << PB3) | (1 << PB4) | (1 << PB5));
  desligar_leds();

  // So o fio testado como saida em 5V
  set_bit(DDRB, fios_cima[cima]);
  set_bit(PORTB, fios_cima[cima]);

  _delay_us(200);

  ler_adc();

  uint16_t valor = ler_adc();

  // Volta os 3 como saidas, com os LEDs acesos
  DDRB |= (1 << PB3) | (1 << PB4) | (1 << PB5);
  ligar_leds();

  fios_ultimo_valor[cima] = valor;   // [TESTE] para o relatorio na serial

  return identificar_fio(valor);
}

// APLICA A PENALIDADE
void aplicar_regra(uint8_t cima, uint8_t baixo) {

  if (cima == 0 && baixo == 2) {
    registrarErro();   // [INTEGRACAO] era penalidade_ms += 5000UL
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

// [INTEGRACAO] os 3 fios estao na cor certa ha pelo menos FIOS_TEMPO_ESTAVEL_MS?
bool fios_todos_corretos(uint32_t agora) {

  for (uint8_t c = 0; c < 3; c++) {

    // CONFERE SE O FIO ESTA NA COR CORRETA
    if (candidato[c] != c || agora - inicio_contato[c] < FIOS_TEMPO_ESTAVEL_MS) {
      return false;
    }
  }

  return true;
}

// FINALIZA O DESAFIO
// [INTEGRACAO] o numero sorteado vai para o LCD e para a serial
// (o display de 7 segmentos mostra o cronometro)
void finalizar_fios() {

  uint8_t resultado = gerar_numero_aleatorio();

  uart_enviar_texto("TODOS OS FIOS CORRETOS! NUMERO: ");
  uart_enviar_numero(resultado);
  uart_enviar_texto("\r\n");

  limparLCD();
  linhaLCD(0);
  textoLCD("FIOS CORRETOS!");
  linhaLCD(1);
  textoLCD("NUMERO: ");
  enviarLCD('0' + resultado, 1);

  // MANTEM OS TRES LEDS LIGADOS
  ligar_leds();

  fios_concluido = true;
}

// [INTEGRACAO] era o setup()
void fios_iniciar() {

  configurar_pinos();
  configurar_adc();

  for (uint8_t c = 0; c < 3; c++) {
    candidato[c] = 255;
    inicio_contato[c] = 0;

    for (uint8_t b = 0; b < 3; b++) {
      registrado[c][b] = false;
    }
  }

  fios_concluido = false;

  ligar_leds();
}

// [INTEGRACAO] era o loop(): devolve 1 quando o desafio foi concluido
byte fios_executar() {

  if (fios_concluido) {
    return 1;
  }

  uint32_t agora = ler_tempo_ms();

  // [FIOS] mede os fios a cada 20 ms. Durante a medida os LEDs piscam
  // apagados; medindo sem parar eles ficavam fracos.
  static uint32_t ultima_medida = 0;

  if (agora - ultima_medida < 20UL) {
    return 0;
  }

  ultima_medida = agora;

  verificar_fios(agora);

#ifdef DESAFIO_TESTE
  // [TESTE] a cada 1 s mostra o valor do A2 e a cor reconhecida de cada fio
  static uint32_t ultimo_relatorio = 0;

  if (agora - ultimo_relatorio >= 1000UL) {

    ultimo_relatorio = agora;

    const char* nomes_cima[3] = {"VERMELHO", " VERDE", " AMARELO"};

    for (uint8_t c = 0; c < 3; c++) {

      uart_enviar_texto(nomes_cima[c]);
      uart_enviar_texto(": A2=");
      uart_enviar_numero(fios_ultimo_valor[c]);
      uart_enviar_texto(" -> ");

      switch (identificar_fio(fios_ultimo_valor[c])) {
        case 0:  uart_enviar_texto("vermelho"); break;
        case 1:  uart_enviar_texto("verde"); break;
        case 2:  uart_enviar_texto("amarelo"); break;
        default: uart_enviar_texto("nada"); break;
      }

      uart_enviar_texto(c < 2 ? " | " : "\r\n");
    }
  }
#endif

  if (fios_todos_corretos(agora)) {

    finalizar_fios();

    return 1;
  }

  return 0;
}

// ========================================================================
// [INTEGRACAO] CONTROLE DOS DESAFIOS
// ========================================================================
// COMO FUNCIONA
//   ordemDesafios guarda a ordem da partida. sortearOrdem() parte da
//   ordem base e embaralha (algoritmo de Fisher-Yates), garantindo que a
//   partida nao comece pelo desafio onde a anterior terminou.
//   iniciarDesafioAtual() desenha a tela e prepara o desafio, e
//   executarDesafioAtual() chama o passo do desafio da vez.

// [SORTEIO] desafio em que a partida anterior terminou sem vitoria
// (explosao ou reinicio no meio). 255 = nenhum.
byte desafioUltimaPartida = 255;

// [SORTEIO] numero de 0 a n-1 com a mesma chance para cada valor
byte sortearIndice(byte n) {

  gerar_numero_aleatorio();   // avanca o gerador

  return semente_aleatoria % n;
}

// Embaralha a ordem dos desafios (nao existe ordem certa entre eles)
// [SORTEIO] cada partida comeca da ordem base, em vez de embaralhar a ordem
// da partida anterior, e o primeiro desafio nunca e aquele em que a
// partida anterior terminou.
void sortearOrdem() {

  semente_aleatoria ^= ler_tempo_ms() ^ ((uint32_t)TCNT1 << 16);

  for (byte i = 0; i < NUM_DESAFIOS; i++) {
    ordemDesafios[i] = i;
  }

  for (byte i = NUM_DESAFIOS - 1; i > 0; i--) {

    byte j = sortearIndice(i + 1);
    byte troca = ordemDesafios[i];

    ordemDesafios[i] = ordemDesafios[j];
    ordemDesafios[j] = troca;
  }

  if (ordemDesafios[0] == desafioUltimaPartida) {

    byte j = 1 + sortearIndice(NUM_DESAFIOS - 1);
    byte troca = ordemDesafios[0];

    ordemDesafios[0] = ordemDesafios[j];
    ordemDesafios[j] = troca;
  }

#ifdef DESAFIO_TESTE
  ordemDesafios[0] = DESAFIO_TESTE;   // [TESTE] so o desafio escolhido
#endif
}

void iniciarDesafioAtual() {

  // Os LEDs de D11-D13 sao compartilhados: comeca sempre apagado
  desligar_leds();

  uart_enviar_texto("DESAFIO ");
  uart_enviar_numero(indiceDesafio + 1);
  uart_enviar_texto("/");
  uart_enviar_numero(TOTAL_DESAFIOS);
  uart_enviar_texto(": ");

  switch (ordemDesafios[indiceDesafio]) {

    case DESAFIO_HEXADECIMAL:
      uart_enviar_linha("HEXADECIMAL");
      limparLCD();
      hexadecimal_iniciar();
      break;

    case DESAFIO_COFRE:
      uart_enviar_linha("COFRE");
      telaDesafio("COFRE", "GIRE 2 POTS");        // [ALTERACAO LCD] era "GIRE OS 2 POTS"
      cofre_iniciar();
      break;

    case DESAFIO_SEQUENCIA:
      uart_enviar_linha("SEQUENCIA");
      telaDesafio("SEQUENCIA", "REPITA LEDS");   // [ALTERACAO LCD] era "REPITA OS LEDS"
      sequencia_iniciar();
      break;

    case DESAFIO_FIOS:
      uart_enviar_linha("FIOS");
      telaDesafio("FIOS", "LIGUE CORES");         // [ALTERACAO LCD] era "LIGUE AS CORES"
      fios_iniciar();
      break;
  }

  tempoLCDPendente = 1;   // [ALTERACAO LCD] tela nova: redesenha o tempo
}

// Devolve 1 quando o desafio atual foi concluido
byte executarDesafioAtual() {

  switch (ordemDesafios[indiceDesafio]) {

    case DESAFIO_HEXADECIMAL:
      return hexadecimal_executar();

    case DESAFIO_COFRE:
      return cofre_executar();

    case DESAFIO_SEQUENCIA:
      return sequencia_executar();

    case DESAFIO_FIOS:
      return fios_executar();
  }

  return 0;
}

// ========================================
// INICIAR O JOGO
// ========================================

void iniciarJogo(byte modo) {

  dificuldade = modo;

  cli();

  tempoRestante = TEMPO_TOTAL_S;
  atualizarTempo = 0;
  cronometroAtivo = 0;

  // Reiniciar a contagem do Timer1
  TCNT1 = 0;
  contadorSegundo = 0;

  // Limpar interrupcao pendente
  TIFR1 = (1 << OCF1A);

  sei();

  mostrarTempo(3, 0);

  telaJogo();
  tempoLCDPendente = 1;   // [ALTERACAO LCD]

  // [INTEGRACAO] prepara os desafios
  erros = 0;
  indiceDesafio = 0;
  desafioIniciado = 0;
  sortearOrdem();
  esperarAte = ler_tempo_ms() + PAUSA_DESAFIOS_MS;

  jogoIniciado = 1;

  cli();
  cronometroAtivo = 1;
  sei();

  uart_enviar_linha("JOGO INICIADO");

  if (modo == 1) {
    uart_enviar_linha("DIFICULDADE: FACIL");
  } else {
    uart_enviar_linha("DIFICULDADE: DIFICIL");
  }
}

// ========================================
// [INTEGRACAO] FIM DE JOGO E REINICIO
// ========================================

void encerrarJogo(byte venceu) {

  cli();
  cronometroAtivo = 0;
  sei();

  jogoEncerrado = 1;
  vitoria = venceu;
  momentoFim = ler_tempo_ms();

  if (venceu) {

    telaVitoria();
    ligar_leds();
    bip(1500, 800);

    uart_enviar_linha("BOMBA DESARMADA! VITORIA");

  } else {

    desligar_leds();

    if (erros >= MAX_ERROS_JOGO) {
      telaExplosao();
      uart_enviar_linha("FIM DE JOGO - 3 ERROS");
    } else {
      mostrarTempo(0, 0);
      telaDerrota();
      uart_enviar_linha("FIM DE JOGO - TEMPO ESGOTADO");
    }

    uart_enviar_linha("BOMBA EXPLODIU! REINICIANDO...");

    // Som grave e longo da explosao
    bip(300, 2000);
  }
}

// [ALTERACAO REINICIO] devolve 1 uma unica vez quando '*' fica apertado por
// TEMPO_REINICIO_MS. Um toque curto continua apagando os digitos no hexadecimal.
// Depois de reiniciar, so vale de novo quando o '*' for solto.
#define TEMPO_REINICIO_MS 1000UL

byte asteriscoSegurado() {

  static byte aguardandoSoltar = 0;

  if (teclaPressionada != '*') {
    aguardandoSoltar = 0;
    return 0;
  }

  if (aguardandoSoltar) {
    return 0;
  }

  if (ler_tempo_ms() - inicioTeclaPressionada >= TEMPO_REINICIO_MS) {
    aguardandoSoltar = 1;
    return 1;
  }

  return 0;
}

// Volta para a escolha da dificuldade (explosao, '*' segurado ou comando 'R')
void reiniciarJogo() {

  // [SORTEIO] guarda o desafio em que a partida parou sem vitoria
  if (jogoIniciado && indiceDesafio < NUM_DESAFIOS) {
    desafioUltimaPartida = ordemDesafios[indiceDesafio];
  } else {
    desafioUltimaPartida = 255;
  }

  cli();
  cronometroAtivo = 0;
  tempoRestante = TEMPO_TOTAL_S;
  atualizarTempo = 0;
  sei();

  meu_noTone();
  bipLigado = 0;

  desligar_leds();
  reiniciar_desafio();   // limpa a flag do 'R'

  jogoIniciado = 0;
  jogoEncerrado = 0;
  vitoria = 0;
  erros = 0;

  mostrarTempo(3, 0);
  telaDificuldade();

  uart_enviar_linha("JOGO REINICIADO");
  uart_enviar_linha("1 = FACIL");
  uart_enviar_linha("2 = DIFICIL");
}

// ========================================
// SETUP
// ========================================

void setup() {

  uart_init();

  Wire.begin();
  Wire.setClock(400000L);   // [OLED] 400 kHz: o OLED redesenha mais rapido

  iniciarTeclado();

  // [INTEGRACAO] pinos dos modulos que precisam estar definidos desde o inicio
  configurar_pinos();               // D11-D13 saidas (apagadas), A2 entrada
  set_bit(BUZZER_DDR, BUZZER_BIT);  // buzzer (A3)
  clr_bit(BUZZER_PORT, BUZZER_BIT);

  iniciarLCD();

  iniciarDisplay();

  mostrarTempo(3, 0);

  iniciarTimer1();

  telaInicial();

  telaDificuldade();

  uart_enviar_linha("ESCAPE ROOM PRONTO");
  uart_enviar_linha("1 = FACIL");
  uart_enviar_linha("2 = DIFICIL");
  uart_enviar_linha("SEGURE * = REINICIAR");
  uart_enviar_linha("R = REINICIAR (SERIAL)");
}

// ========================================
// LOOP PRINCIPAL
// ========================================
// COMO FUNCIONA
//   A cada volta: trata o reinicio ('R' ou * segurado), le o teclado,
//   decide pelo estado do jogo (escolha do modo, jogando ou fim de jogo),
//   atualiza o cronometro e chama um passo do desafio atual. Nada aqui
//   espera: cada parte faz o que tem para fazer e devolve o controle.

void loop() {

  // [INTEGRACAO] comando 'R' recebido pela UART
  if (reiniciar_solicitado) {
    reiniciarJogo();
    return;
  }

  atualizarBip();

  char tecla = lerTeclado();

  // [ALTERACAO REINICIO] segurar '*' reinicia o jogo em qualquer momento
  if (asteriscoSegurado()) {
    uart_enviar_linha("REINICIO PELO TECLADO");
    reiniciarJogo();
    return;
  }

  // [INTEGRACAO] fim de jogo: explosao recomeca sozinha,
  // vitoria espera '*' segurado (ou 'R' pela serial)
  if (jogoEncerrado) {

    if (!vitoria && ler_tempo_ms() - momentoFim >= TEMPO_EXPLOSAO_MS) {
      reiniciarJogo();
    }

    return;
  }

  // Antes de iniciar o jogo
  if (!jogoIniciado) {

    if (tecla == '1') {
      iniciarJogo(1);
    }

    else if (tecla == '2') {
      iniciarJogo(2);
    }

    return;
  }

  // Cronometro, aviso sonoro e condicao de explosao
  if (!servicoBomba()) {
    encerrarJogo(0);
    return;
  }

  // Teste das teclas durante o jogo
  if (tecla != '\0') {

    uart_enviar_texto("TECLA: ");
    uart_enviar_char(tecla);
    uart_enviar_texto("\r\n");
  }

  // [INTEGRACAO] pausa com a mensagem entre um desafio e outro
  if ((int32_t)(ler_tempo_ms() - esperarAte) < 0) {
    return;
  }

  if (!desafioIniciado) {
    iniciarDesafioAtual();
    desafioIniciado = 1;
  }

  teclaJogo = tecla;

  if (executarDesafioAtual()) {

    uart_enviar_linha("DESAFIO CONCLUIDO");

    // O hexadecimal e os fios ja escrevem a propria mensagem de sucesso
    byte d = ordemDesafios[indiceDesafio];

    if (d == DESAFIO_COFRE || d == DESAFIO_SEQUENCIA) {
      linhaLCD(1);
      textoLCD("CONCLUIDO! ");   // [ALTERACAO LCD] 11 caracteres, o resto e do tempo
    }

    tempoLCDPendente = 1;   // [ALTERACAO LCD] a mensagem pode ter apagado o tempo

    indiceDesafio++;
    desafioIniciado = 0;

    if (indiceDesafio >= TOTAL_DESAFIOS) {
      encerrarJogo(1);
      return;
    }

    esperarAte = ler_tempo_ms() + PAUSA_DESAFIOS_MS;
  }
}
