// TESTE 3: descobre como o teclado 4x3 esta ligado, sem supor ordem de pinos.
// Ligue os 7 terminais do teclado em D3 a D9 (qualquer ordem) e aperte uma
// tecla de cada vez. O teste mostra quais dois pinos se ligam.
//
// No jogo, o esperado e:
//   colunas: D3 = 1 4 7 * | D4 = 2 5 8 0 | D5 = 3 6 9 #
//   linhas:  D6 = 1 2 3 | D7 = 4 5 6 | D8 = 7 8 9 | D9 = * 0 #
// Exemplo: a tecla 5 deve mostrar "D4 - D7", e a tecla 1 "D3 - D6".
#include <Arduino.h>

const byte pinos[7] = {3, 4, 5, 6, 7, 8, 9};

// Mapa do jogo (coluna D3..D5, linha D6..D9)
const char teclas[4][3] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};

void todosEntradaPullup() {
  for (byte i = 0; i < 7; i++) pinMode(pinos[i], INPUT_PULLUP);
}

void setup() {
  Serial.begin(9600);
  todosEntradaPullup();
  Serial.println(F("TESTE 3 - TECLADO: aperte uma tecla de cada vez"));
}

void loop() {
  static char ultimo[40] = "";
  char atual[40] = "";

  // Coloca um pino por vez em LOW e ve quais outros pinos caem para LOW
  for (byte i = 0; i < 7; i++) {
    todosEntradaPullup();
    pinMode(pinos[i], OUTPUT);
    digitalWrite(pinos[i], LOW);
    delayMicroseconds(50);

    for (byte j = i + 1; j < 7; j++) {
      if (digitalRead(pinos[j]) == LOW) {
        byte a = pinos[i], b = pinos[j];
        char tecla = '?';
        if (a >= 3 && a <= 5 && b >= 6 && b <= 9) tecla = teclas[b - 6][a - 3];
        snprintf(atual + strlen(atual), sizeof(atual) - strlen(atual),
                 "D%d - D%d (jogo leria '%c')  ", a, b, tecla);
      }
    }
  }
  todosEntradaPullup();

  if (strcmp(atual, ultimo) != 0) {
    if (atual[0]) Serial.println(atual);
    else Serial.println(F("(solto)"));
    strcpy(ultimo, atual);
  }

  delay(30);
}
