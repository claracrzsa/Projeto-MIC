# Projeto-MIC
#include <Wire.h>
#include <avr/io.h>
#include <avr/interrupt.h>

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

// ========================================
// TECLADO 4X4
// ========================================

// Terminais do teclado:
// 1 -> D2
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

void iniciarTeclado() {

  // D2 a D5: saidas
  DDRD |= 0b00111100;

  // D6 e D7: entradas
  DDRD &= ~0b11000000;

  // D8 e D9: entradas
  DDRB &= ~0b00000011;

  // Linhas em HIGH
  PORTD |= 0b00111100;

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

  for (byte linha = 0; linha < 4; linha++) {

    // Todas as linhas em HIGH
    PORTD |= 0b00111100;

    // Somente uma linha em LOW
    PORTD &= ~(1 << (linha + 2));

    delayMicroseconds(100);

    for (byte coluna = 0; coluna < 4; coluna++) {

      if (colunaPressionada(coluna)) {

        PORTD |= 0b00111100;

        return teclas[linha][coluna];
      }
    }
  }

  PORTD |= 0b00111100;

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
// P4-P7 = DB4-DB7

void enviarPCF(byte valor) {

  Wire.beginTransmission(LCD_ADDR);
  Wire.write(valor);
  Wire.endTransmission();
}

void pulsoLCD(byte valor) {

  enviarPCF(valor | 0x04);
  delayMicroseconds(100);

  enviarPCF(valor & 0xFB);
  delayMicroseconds(100);
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
  delay(2);
}

void textoLCD(const char* texto) {

  while (*texto) {

    enviarLCD(*texto, 1);
    texto++;
  }
}

void iniciarLCD() {

  delay(100);

  nibbleLCD(0x30, 0);
  delay(5);

  nibbleLCD(0x30, 0);
  delay(5);

  nibbleLCD(0x30, 0);
  delay(1);

  nibbleLCD(0x20, 0);
  delay(5);

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

  delay(1500);
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
// TIMER1 - INTERRUPCAO A CADA 1 SEGUNDO
// ========================================

void iniciarTimer1() {

  cli();

  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;

  // 16 MHz / 1024 = 15625 contagens
  OCR1A = 15624;

  // Modo CTC
  TCCR1B |= (1 << WGM12);

  // Prescaler 1024
  TCCR1B |= (1 << CS12);
  TCCR1B |= (1 << CS10);

  // Habilitar interrupcao
  TIMSK1 |= (1 << OCIE1A);

  sei();
}

ISR(TIMER1_COMPA_vect) {

  if (cronometroAtivo && tempoRestante > 0) {

    tempoRestante--;

    atualizarTempo = 1;

    if (tempoRestante == 0) {
      cronometroAtivo = 0;
    }
  }
}

// ========================================
// INICIAR O JOGO
// ========================================

void iniciarJogo(byte modo) {

  dificuldade = modo;

  cli();

  tempoRestante = 180;
  atualizarTempo = 0;
  cronometroAtivo = 0;

  // Reiniciar a contagem do Timer1
  TCNT1 = 0;

  // Limpar interrupcao pendente
  TIFR1 = (1 << OCF1A);

  sei();

  mostrarTempo(3, 0);

  telaJogo();

  jogoIniciado = 1;

  cli();
  cronometroAtivo = 1;
  sei();

  Serial.println("JOGO INICIADO");

  if (modo == 1) {
    Serial.println("DIFICULDADE: FACIL");
  } else {
    Serial.println("DIFICULDADE: DIFICIL");
  }
}

// ========================================
// SETUP
// ========================================

void setup() {

  Serial.begin(9600);

  Wire.begin();

  iniciarTeclado();

  iniciarLCD();

  iniciarDisplay();

  mostrarTempo(3, 0);

  iniciarTimer1();

  telaInicial();

  telaDificuldade();

  Serial.println("ESCAPE ROOM PRONTO");
  Serial.println("1 = FACIL");
  Serial.println("2 = DIFICIL");
}

// ========================================
// LOOP PRINCIPAL
// ========================================

void loop() {

  if (jogoEncerrado) {
    return;
  }

  char tecla = lerTeclado();

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
  }

  // Tempo esgotado
  if (tempoAtual == 0) {

    cli();
    cronometroAtivo = 0;
    sei();

    mostrarTempo(0, 0);

    telaDerrota();

    Serial.println("FIM DE JOGO");
    Serial.println("TEMPO ESGOTADO");

    jogoEncerrado = 1;

    return;
  }

  // Teste das teclas durante o jogo
  if (tecla != '\0') {

    Serial.print("TECLA: ");
    Serial.println(tecla);
  }
}
