// ========================================================================
// ESCAPE ROOM - INTEGRACAO DE TODOS OS MODULOS EM UM UNICO ARDUINO UNO
// ========================================================================
//
// Este arquivo junta, em um so sketch:
//   bomba.ino                -> fluxo do jogo (dificuldade, cronometro, LCD, display)
//   binario_decimal1 (2).ino -> desafio HEX -> DECIMAL (teclado + LCD)
//   cofre.ino                -> desafio do cofre (2 potenciometros)
//   desafio_sequencia.ino    -> desafio da sequencia (2 LEDs + 2 botoes)
//   fios.ino                 -> desafio dos fios (3 fios + leitura no ADC)
//   buzzer.ino               -> aviso sonoro
//
// O codigo de cada modulo foi mantido o mais proximo possivel do original.
// Os trechos alterados para a integracao estao marcados com "[INTEGRACAO]".
// O mapa de pinos e as mudancas estao explicados no README.md.
//
// MAPA DE PINOS
//   D0/D1  UART (comando 'R' reinicia o jogo)
//   D2     BOTAO1 da sequencia (nao mudou), interrupcao externa INT0
//   D3-D5  linhas do teclado (terminais 2, 3 e 4)
//   D6-D9  colunas do teclado (terminais 5 a 8, pull-up externo de 10k)
//   D10    BOTAO2 da sequencia (era D3), interrupcao PCINT2
//   D11    fio superior amarelo
//   D12    fio superior verde    + LED2 da sequencia / LED do cofre 2
//   D13    fio superior vermelho + LED1 da sequencia / LED do cofre 1
//   A0     potenciometro do cofre 1
//   A1     potenciometro do cofre 2
//   A2     leitura dos fios inferiores (era A1)
//   A3     buzzer (era D11)
//   A4/A5  I2C: LCD PCF8574 (0x20) e display HT16K33 (0x70)
// ========================================================================

#include <Wire.h>
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

// ========================================================================
// MODULO COMPARTILHADO: BASE DE TEMPO, UART E BUZZER
// ========================================================================

// ----------------------------------------
// BASE DE TEMPO DE 1 ms (Timer1)
// ----------------------------------------
// [INTEGRACAO] todos os modulos usavam o Timer1 com uma ISR propria
// (1 s na bomba, 1 ms nos fios/cofre/sequencia, 10 ms no binario).
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

#define LCD_ADDR 0x20
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
#define DESAFIO_BINARIO   0
#define DESAFIO_COFRE     1
#define DESAFIO_SEQUENCIA 2
#define DESAFIO_FIOS      3

byte erros = 0;                     // erros somados de todos os desafios
byte ordemDesafios[NUM_DESAFIOS] = {
  DESAFIO_BINARIO, DESAFIO_COFRE, DESAFIO_SEQUENCIA, DESAFIO_FIOS
};
byte indiceDesafio = 0;             // posicao atual em ordemDesafios
byte desafioIniciado = 0;
byte vitoria = 0;
uint32_t esperarAte = 0;            // pausa entre desafios
uint32_t momentoFim = 0;
char teclaJogo = '\0';              // tecla lida neste loop, repassada ao binario

volatile uint16_t contadorSegundo = 0;

uint32_t fimBip = 0;
byte bipLigado = 0;

// ========================================
// TECLADO 4X4
// ========================================

// Terminais do teclado:
// 1 -> desligado  [INTEGRACAO] era D2 (linha das letras A-D, nao usada)
// 2 -> D3
// 3 -> D4
// 4 -> D5
// 5 -> D6
// 6 -> D7
// 7 -> D8
// 8 -> D9
//
// Resistores de 10k entre:
// D6 e 5V
// D7 e 5V
// D8 e 5V
// D9 e 5V

const char teclas[4][4] = {
  {'D', 'C', 'B', 'A'},
  {'#', '9', '6', '3'},
  {'0', '8', '5', '2'},
  {'*', '7', '4', '1'}
};

// [INTEGRACAO] mascara das linhas: D3 a D5 (D2 ficou para o BOTAO1)
#define LINHAS_TECLADO 0b00111000

void iniciarTeclado() {

  // D3 a D5: saidas
  DDRD |= LINHAS_TECLADO;

  // D6 e D7: entradas
  DDRD &= ~0b11000000;

  // D8 e D9: entradas
  DDRB &= ~0b00000011;

  // Linhas em HIGH
  PORTD |= LINHAS_TECLADO;

  // Pull-ups internos desativados
  // Usaremos os resistores externos
  PORTD &= ~0b11000000;
  PORTB &= ~0b00000011;
}

bool colunaPressionada(byte coluna) {

  switch (coluna) {

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

char lerTecladoBruto() {

  // [INTEGRACAO] comeca na linha 1 (a linha 0 estava no D2)
  for (byte linha = 1; linha < 4; linha++) {

    // Todas as linhas em HIGH
    PORTD |= LINHAS_TECLADO;

    // Somente uma linha em LOW
    PORTD &= ~(1 << (linha + 2));

    _delay_us(100);

    for (byte coluna = 0; coluna < 4; coluna++) {

      if (colunaPressionada(coluna)) {

        PORTD |= LINHAS_TECLADO;

        return teclas[linha][coluna];
      }
    }
  }

  PORTD |= LINHAS_TECLADO;

  return '\0';
}

// Uma leitura por pressionamento
char lerTeclado() {

  static char ultimaLeitura = '\0';
  static char teclaEstavel = '\0';
  static byte contador = 0;

  char atual = lerTecladoBruto();

  if (atual == ultimaLeitura) {

    if (contador < 4) {
      contador++;
    }

  } else {

    ultimaLeitura = atual;
    contador = 0;
  }

  if (contador >= 4 && atual != teclaEstavel) {

    teclaEstavel = atual;

    if (teclaEstavel != '\0') {
      return teclaEstavel;
    }
  }

  return '\0';
}

// ========================================
// LCD 16X2 COM PCF8574
// ========================================

// P0 = RS
// P2 = E
// P3 = luz de fundo
// P4-P7 = DB4-DB7

void enviarPCF(byte valor) {

  Wire.beginTransmission(LCD_ADDR);
  // [INTEGRACAO] mantem a luz de fundo (P3) ligada, como no binario
  Wire.write(valor | 0x08);
  Wire.endTransmission();
}

void pulsoLCD(byte valor) {

  enviarPCF(valor | 0x04);
  _delay_us(100);

  enviarPCF(valor & 0xFB);
  _delay_us(100);
}

void nibbleLCD(byte valor, byte rs) {

  pulsoLCD((valor & 0xF0) | rs);
}

void enviarLCD(byte valor, byte rs) {

  nibbleLCD(valor, rs);
  nibbleLCD(valor << 4, rs);
}

void comandoLCD(byte valor) {

  enviarLCD(valor, 0);
  _delay_ms(2);
}

void textoLCD(const char* texto) {

  while (*texto) {

    enviarLCD(*texto, 1);
    texto++;
  }
}

void iniciarLCD() {

  _delay_ms(100);

  nibbleLCD(0x30, 0);
  _delay_ms(5);

  nibbleLCD(0x30, 0);
  _delay_ms(5);

  nibbleLCD(0x30, 0);
  _delay_ms(1);

  nibbleLCD(0x20, 0);
  _delay_ms(5);

  comandoLCD(0x28);
  comandoLCD(0x0C);
  comandoLCD(0x06);
  comandoLCD(0x01);
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

  if (dificuldade == 1) {
    textoLCD("MODO: FACIL");
  } else {
    textoLCD("MODO: DIFICIL");
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
  textoLCD("# PARA REINICIAR");
}

// Cabecalho de cada desafio: "1/4 NOME" e uma dica na linha de baixo
void telaDesafio(const char* nome, const char* dica) {

  limparLCD();

  linhaLCD(0);
  enviarLCD('1' + indiceDesafio, 1);
  textoLCD("/4 ");
  textoLCD(nome);

  linhaLCD(1);
  textoLCD(dica);
}

// ========================================
// DISPLAY HT16K33
// ========================================

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

  atualizarBip();

  return (tempoAtual > 0 && erros < MAX_ERROS_JOGO);
}

// ========================================================================
// DESAFIO 1: BINARIO / HEXADECIMAL -> DECIMAL (binario_decimal1 (2).ino)
// ========================================================================
// [INTEGRACAO]
// - O I2C passa a ser feito pela biblioteca Wire (a mesma da bomba), em vez
//   do TWI por registradores, para nao misturar dois drivers no mesmo barramento.
// - O LCD ja e inicializado pela bomba (lcd_init removido).
// - O teclado proprio (D2-D5 / D8-D10) foi removido: o binario usa o teclado
//   4x4 da bomba, cuja tecla e lida no loop principal (teclaJogo).
// - Os erros agora sao os erros do jogo (registrarErro), nao locais.

#define PCF_ENDERECO      LCD_ADDR   // PCF8574 com A0, A1 e A2 no GND

// bits do PCF8574 ligados ao LCD
#define LCD_RS            0x01   // P0
#define LCD_E             0x04   // P2
#define LCD_LUZ           0x08   // P3 (luz de fundo, se estiver ligada)
// P4 a P7 levam os dados D4 a D7 do LCD

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

// [INTEGRACAO] envia dois bytes ao PCF8574 usando a Wire
static void pcf_escreve_dois(uint8_t primeiro, uint8_t segundo) {
  Wire.beginTransmission(PCF_ENDERECO);
  Wire.write(primeiro);
  Wire.write(segundo);
  Wire.endTransmission();
}


static void lcd_nibble(uint8_t nibble, uint8_t rs) {
  uint8_t dado = (nibble << 4) | LCD_LUZ | rs;
  pcf_escreve_dois(dado | LCD_E, dado & ~LCD_E);
}


static void lcd_byte(uint8_t valor, uint8_t rs) {
  lcd_nibble(valor >> 4, rs);
  lcd_nibble(valor & 0x0F, rs);
}

// envia um comando ao LCD. limpar e voltar ao início levam cerca de 2 ms
static void lcd_comando(uint8_t comando) {
  lcd_byte(comando, 0);
  if (comando <= 0x03) _delay_ms(2);
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
// completa o campo com espaços e apaga a mensagem "Errou!" da linha 1
void display_mostra_digitado(uint16_t valor, uint8_t qtd_digitos) {
  uint8_t n = 0;
  if (qtd_digitos > 0) n = (valor < 10) ? 1 : (valor < 100) ? 2 : 3;
  lcd_posicao(0, 1);
  lcd_texto("Dec:");
  if (qtd_digitos > 0) lcd_numero(valor);
  for (uint8_t i = 4 + n; i < 7; i++) lcd_caractere(' ');
  lcd_posicao(9, 0);
  for (uint8_t i = 9; i < 16; i++) lcd_caractere(' ');
}

// mostra a quantidade de erros no canto direito da linha 2 (ex.: "Erros:1")
// [INTEGRACAO] mostra os erros do jogo inteiro
static void mostra_erros(void) {
  lcd_posicao(7, 1);
  lcd_texto("Erros:");
  lcd_numero(erros);
}

// [INTEGRACAO] a tecla vem do teclado 4x4 da bomba (lida uma vez por loop)
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
void binario_iniciar() {
  estado_desafio = DESAFIO_ANDAMENTO;
  fase_teclado_inicia((uint16_t)ler_tempo_ms() ^ TCNT1);
}

// [INTEGRACAO] era o loop(): devolve 1 quando o desafio foi concluido.
// O terceiro erro agora e tratado pela bomba (explosao).
byte binario_executar() {
  if (estado_desafio != DESAFIO_ANDAMENTO) return 1;

  uint8_t r = fase_teclado_atualiza();
  if (r == FASE_ACERTOU) {
    estado_desafio = DESAFIO_CONCLUIDO;
    lcd_linha(0, "Parabens, passou");
    lcd_linha(1, "p/ proxima fase");
    return 1;
  } else if (r == FASE_ERROU) {
    lcd_posicao(9, 0);
    lcd_texto("Errou!");
  }
  return 0;
}

// ========================================================================
// DESAFIO 2: COFRE (cofre.ino)
// ========================================================================
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

// Estados da maquina
#define SEQ_APRESENTANDO   0   // era apresentar_sequencia()
#define SEQ_PAUSA          1   // era espera_ms(500)
#define SEQ_ESPERA_BOTAO   2   // era ler_botao(): espera a flag da interrupcao
#define SEQ_CONFIRMA_BOTAO 3   // era ler_botao(): debounce e nova leitura
#define SEQ_SOLTAR         4   // era aguardar_soltar(): espera soltar
#define SEQ_SOLTAR_REPIQUE 5   // era aguardar_soltar(): repique na liberacao
#define SEQ_ERRO           6   // era indicar_erro() + espera_ms(1000)
#define SEQ_VITORIA        7   // era indicar_vitoria()

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

  seq_ir_para(SEQ_APRESENTANDO);
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
      } else if (seq_decorrido() >= SEQ_DEBOUNCE) {
        // Ignora as interrupcoes geradas pelo repique
        botao_evento = 0;

        if (seq_resposta == 0) {
          seq_ir_para(SEQ_ESPERA_BOTAO);
        } else {
          seq_conferir_resposta();
        }
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
        seq_ir_para(SEQ_APRESENTANDO);
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

  verificar_fios(agora);

  if (fios_todos_corretos(agora)) {

    finalizar_fios();

    return 1;
  }

  return 0;
}

// ========================================================================
// [INTEGRACAO] CONTROLE DOS DESAFIOS
// ========================================================================

// Embaralha a ordem dos desafios (nao existe ordem certa entre eles)
void sortearOrdem() {

  semente_aleatoria ^= ler_tempo_ms();

  for (byte i = NUM_DESAFIOS - 1; i > 0; i--) {

    byte j = gerar_numero_aleatorio() % (i + 1);
    byte troca = ordemDesafios[i];

    ordemDesafios[i] = ordemDesafios[j];
    ordemDesafios[j] = troca;
  }
}

void iniciarDesafioAtual() {

  // Os LEDs de D11-D13 sao compartilhados: comeca sempre apagado
  desligar_leds();

  uart_enviar_texto("DESAFIO ");
  uart_enviar_numero(indiceDesafio + 1);
  uart_enviar_texto("/4: ");

  switch (ordemDesafios[indiceDesafio]) {

    case DESAFIO_BINARIO:
      uart_enviar_linha("BINARIO");
      limparLCD();
      binario_iniciar();
      break;

    case DESAFIO_COFRE:
      uart_enviar_linha("COFRE");
      telaDesafio("COFRE", "GIRE OS 2 POTS");
      cofre_iniciar();
      break;

    case DESAFIO_SEQUENCIA:
      uart_enviar_linha("SEQUENCIA");
      telaDesafio("SEQUENCIA", "REPITA OS LEDS");
      sequencia_iniciar();
      break;

    case DESAFIO_FIOS:
      uart_enviar_linha("FIOS");
      telaDesafio("FIOS", "LIGUE AS CORES");
      fios_iniciar();
      break;
  }
}

// Devolve 1 quando o desafio atual foi concluido
byte executarDesafioAtual() {

  switch (ordemDesafios[indiceDesafio]) {

    case DESAFIO_BINARIO:
      return binario_executar();

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

// Volta para a escolha da dificuldade (explosao, vitoria ou comando 'R')
void reiniciarJogo() {

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
  uart_enviar_linha("R = REINICIAR");
}

// ========================================
// LOOP PRINCIPAL
// ========================================

void loop() {

  // [INTEGRACAO] comando 'R' recebido pela UART
  if (reiniciar_solicitado) {
    reiniciarJogo();
    return;
  }

  atualizarBip();

  char tecla = lerTeclado();

  // [INTEGRACAO] fim de jogo: explosao recomeca sozinha,
  // vitoria espera '#' (ou 'R' pela serial)
  if (jogoEncerrado) {

    if (!vitoria && ler_tempo_ms() - momentoFim >= TEMPO_EXPLOSAO_MS) {
      reiniciarJogo();
    } else if (vitoria && tecla == '#') {
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

    // O binario e os fios ja escrevem a propria mensagem de sucesso
    byte d = ordemDesafios[indiceDesafio];

    if (d == DESAFIO_COFRE || d == DESAFIO_SEQUENCIA) {
      linhaLCD(1);
      textoLCD("CONCLUIDO!      ");
    }

    indiceDesafio++;
    desafioIniciado = 0;

    if (indiceDesafio >= NUM_DESAFIOS) {
      encerrarJogo(1);
      return;
    }

    esperarAte = ler_tempo_ms() + PAUSA_DESAFIOS_MS;
  }
}
