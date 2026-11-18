#include <avr/io.h>

#define set_bit(reg, bit) ((reg) |= (1 << (bit)))
#define tst_bit(reg, bit) ((reg) & (1 << (bit)))
#define clr_bit(reg, bit) ((reg) &= ~(1 << (bit)))

#define LIMIAR_ALTO1 700 // acima disso: liga
#define LIMIAR_BAIXO1 600 // abaixo disso: desliga

#define LIMIAR_ALTO2 400 // acima disso: liga
#define LIMIAR_BAIXO2 300 // abaixo disso: desliga

uint16_t leitura_A0 = 0;
uint16_t leitura_A1 = 0;

uint8_t canal = 0;

uint8_t alavanca_ligada = 0;

void atualiza_alavanca1(uint16_t valor) {
  if ( LIMIAR_BAIXO1 < valor && valor < LIMIAR_ALTO1){
    alavanca_ligada = 1;
    set_bit(PORTD, PD5);
  } else {
    alavanca_ligada = 0;
    clr_bit(PORTD, PD5);
  }
  
  // entre 600 e 700: não muda
  
}

void atualiza_alavanca2(uint16_t valor) {
  if ( LIMIAR_BAIXO2 < valor && valor < LIMIAR_ALTO2){
    alavanca_ligada = 1;
    set_bit(PORTD, PD6);
  } else {
    alavanca_ligada = 0;
    clr_bit(PORTD, PD6);
  }
  
   // entre 300 e 400: não muda
}


void setup()
{
  Serial.begin(9600); 
  ADMUX = (1 << REFS0); // Referência 5V, canal 0
  // Clock: 16 / 128
  ADCSRA = (1 << ADEN)
         | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
  set_bit(ADCSRA, ADSC); // Primeira conversão
}

void loop(){
    if (!tst_bit(ADCSRA, ADSC)) // Conversão terminou?
    {
        switch (canal)
    	{
          case 0:
              leitura_A0 = ADC;
              canal = 1;
              break;
          case 1:
              leitura_A1 = ADC;
              canal = 0;
              break;
        }
        ADMUX = (1 << REFS0) | canal; // Seleciona próximo canal
        
      	set_bit(ADCSRA, ADSC); // Inicia próxima conversão
    }
 
    atualiza_alavanca1(leitura_A0); 
    atualiza_alavanca2(leitura_A1); 
    Serial.println(leitura_A0);
    Serial.println(leitura_A1);
}

