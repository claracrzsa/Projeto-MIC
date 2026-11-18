volatile uint8_t flag_1s = 0;	/* volatile: muda dentro da interrupção */

ISR(TIMER1_COMPA_vect)		/* chamada quando TCNT1 chega em OCR1A */
{
flag_1s = 1;
}

void setup()
{
cli();
DDRB |= (1 << DDB5);		/* LED em D13 como saída */

TCCR1A = 0;					/* nenhum pino ligado ao timer */
TCCR1B = 0;					/* timer parado durante a configuração */
OCR1A = 62499;				/* alvo: 1 s */
TIMSK1 = (1 << OCIE1A);		/* liga a interrupção de comparação A */
TCCR1B = (1 << WGM12) | (1 << CS12); /* modo CTC + prescaler 256: começa a contar */
sei();						/* liga as interrupções */
}

void loop()
{
if(flag_1s){
flag_1s=0; /*abaixaaflag*/
PORTB ^=(1<<PORTB5); /*inverteoLED*/
}
}