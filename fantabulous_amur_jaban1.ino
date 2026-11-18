volatile uint8_t flag_500ms=0;

ISR(TIMER2_COMPA_vect) /*chamadaacada1ms*/
{
  static uint16_t contador_ms=0; /*contaosmilissegundos*/
  contador_ms++;
  if(contador_ms>=500){ /*passaram500ms*/
    contador_ms=0;
    flag_500ms=1;
  }
}

void setup()
{
  cli();
  DDRB|= (1<<DDB5); /*LEDemD13comosaída*/
  
  TCCR2A=(1<<WGM21); /*modoCTC*/
  TCCR2B=0; /*timerparadoduranteaconfiguração*/
  OCR2A =249; /*alvo:1ms(contadaseção4)*/
  TIMSK2=(1<<OCIE2A); /*ligaainterrupçãodecomparaçãoA*/
  TCCR2B=(1<<CS22); /*prescaler64:começaacontar*/
  sei();
}
void loop()
{
  if(flag_500ms){
    flag_500ms=0;
    PORTB ^=(1<<PORTB5); /*inverteoLEDacada500ms*/
	}
}