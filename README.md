# Projeto MIC: Escape Room com Arduino Uno

Projeto final de DAS 5332 (Arquitetura e Programação de Sistemas Microcontrolados, UFSC).
O jogador precisa desarmar uma bomba resolvendo 4 desafios antes que o cronômetro chegue a zero.

## Estrutura

| Arquivo ou pasta | Conteúdo |
|---|---|
| `escape_room_plat_io/` | Projeto PlatformIO: é o que vai para o Arduino |
| `escape_room_plat_io/src/main.cpp` | Jogo completo, com os 4 desafios integrados |
| `escape_room_plat_io/src/testes/` | Testes de bancada, um por componente |
| `escape_room_plat_io/platformio.ini` | Ambientes de compilação: jogo, testes e um desafio por vez |
| `escape_room/escape_room.ino` | Cópia do `main.cpp` para quem preferir a IDE do Arduino |
| `bomba.ino`, `binario_decimal1 (2).ino`, `cofre.ino`, `desafio_sequencia.ino`, `fios.ino`, `buzzer.ino` | Códigos originais de cada integrante, mantidos como referência. O `binario_decimal1 (2).ino` é o desafio hexadecimal |
| `clock1ms.ino`, `clock2s.ino` | Testes de timer (não usados na integração) |
| `Registro_IA_Escape_Room.pdf` | Declaração de uso de IA com todos os prompts |
| `Projeto_final_MIC (1).pdf` | Enunciado |

Observação: o `bomba.ino` original está com um conflito de merge salvo no arquivo (linhas `=======` e `>>>>>>> main`, com um pedaço do `fios.ino` no meio). A integração usou apenas a parte da bomba.

## Como gravar

Com o PlatformIO (VS Code, terminal "PlatformIO: New Terminal"), dentro de `escape_room_plat_io`:

```
pio run -e uno -t upload        # jogo completo
pio device monitor              # monitor serial, 9600 baud
```

A porta está configurada como `COM7` no `platformio.ini` (`upload_port` e `monitor_port`). Feche o monitor (`Ctrl+C`) antes de cada upload.

O PlatformIO baixa sozinho a biblioteca do OLED (`greiman/SSD1306Ascii`). Na IDE do Arduino, abra `escape_room/escape_room.ino` e instale antes a biblioteca SSD1306Ascii pelo gerenciador de bibliotecas.

### Ambientes do `platformio.ini`

| Ambiente | O que grava |
|---|---|
| `uno` | Jogo completo (padrão) |
| `jogo_fios`, `jogo_cofre`, `jogo_sequencia`, `jogo_hexadecimal` | O jogo com um desafio só, para testar cada um. Nos fios, a serial mostra a leitura do A2 de cada fio a cada 1 s |
| `teste_1_i2c` | Procura os dispositivos I2C. Esperado: `0x3C` (OLED) e `0x70` (display de 7 segmentos) |
| `teste_2_oled` | Escreve no OLED com o mesmo layout do jogo |
| `teste_3_teclado` | Mostra quais pinos se ligam a cada tecla, sem supor a ordem dos terminais |
| `teste_4_componentes` | Pisca os LEDs, bipa o buzzer e mostra A0, A1, A2 e os botões na serial |
| `teste_5_buzzer` | Liga e desliga o buzzer pelo Timer2 e com 5V fixo, para saber se ele é passivo ou ativo |

Os testes de bancada usam funções do Arduino (`Serial`, `pinMode`, `tone`) por serem só ferramentas de diagnóstico. O jogo usa apenas registradores.

## Como jogar

1. Ao ligar, o OLED mostra `ESCAPE ROOM` e depois `ESCOLHA O MODO`. Aperte `1` (fácil) ou `2` (difícil) no teclado.
2. O cronômetro começa em 3:00. Ele aparece no display de 7 segmentos e no canto direito da linha de baixo do OLED.
3. Os 4 desafios aparecem um de cada vez, em ordem sorteada a cada partida. O OLED mostra qual é o desafio (`1/4 COFRE`, por exemplo).
4. Concluir os 4 desafios desarma a bomba (`BOMBA DESARMADA`), com bip agudo e LEDs acesos.

Para reiniciar a qualquer momento, segure a tecla `*` por 1 segundo ou envie `R` pela serial. Um toque curto no `*` não reinicia: no desafio hexadecimal ele apaga os dígitos digitados.

Regras de erro:

| | Fácil | Difícil |
|---|---|---|
| Penalidade por erro | 5 s | 10 s |
| Erros até explodir | 3 | 3 |

Os erros são somados entre todos os desafios. No terceiro erro, ou se o tempo acabar, a bomba explode: o OLED mostra a explosão, o buzzer toca um som grave e, depois de 3 s, o jogo volta para a escolha de dificuldade. A partida seguinte nunca começa pelo desafio em que a anterior terminou.

Nos últimos 30 s o buzzer apita a cada segundo.

### Desafios

| Desafio | Objetivo | O que conta como erro |
|---|---|---|
| Hexadecimal | Converter para decimal o número hexadecimal mostrado no OLED. Dígitos digitam, `*` apaga, `#` confirma | Confirmar um valor errado |
| Cofre | Girar os 2 potenciômetros até as faixas certas (A0 entre 600 e 700, A1 entre 300 e 400) e manter por 1 s. Cada LED acende quando o seu potenciômetro está na faixa | Não tem erro |
| Sequência | A tela mostra `PRESSIONE BOTAO`. Ao apertar qualquer botão, os 2 LEDs piscam e a sequência 1, 2, 2, 1, 2 é apresentada. O jogador repete nos 2 botões; cada clique acende o LED do botão | Apertar o botão errado. Depois de um erro, a rodada recomeça pelo `PRESSIONE BOTAO` |
| Fios | Ligar cada fio de cima no terminal de baixo da mesma cor e manter por 0,5 s. Ao concluir, o OLED mostra um número sorteado de 1 a 9 | Ligar o vermelho de cima no amarelo de baixo (conta uma vez por partida) |

### Telas do OLED

O OLED de 128x64 pixels imita o LCD 16x2 original: a fonte de 8x16 pixels dá 16 colunas por linha. A linha de cima fica no topo da tela e a de baixo no meio. Durante o jogo, as colunas 12 a 15 da linha de baixo mostram o tempo (`M:SS`):

```
DESAFIOS ATIVOS        1/4 COFRE              Hex:0x2F Erros:1
MODO: FACIL 3:00       GIRE 2 POTS 2:41       Dec:47      2:13
```

As telas de escolha de modo, explosão e vitória não mostram o tempo, porque o cronômetro está parado.

### Comandos pela serial (9600 baud, 8N1)

| Comando | Efeito |
|---|---|
| `R` ou `r` | Reinicia o jogo a qualquer momento (o enunciado exige reinício por comando serial) |

O jogo também envia mensagens de acompanhamento: início, desafio atual (`DESAFIO 1/4: COFRE`), teclas, erros, leituras do cofre e fim de jogo.

## Pinagem

Os 4 desafios e a bomba, cada um com seus pinos originais, precisariam de mais pinos do que o Uno tem. Por isso os desafios rodam um de cada vez, e alguns pinos são compartilhados entre desafios que nunca estão ativos ao mesmo tempo.

| Pino | Função |
|---|---|
| D0 / D1 | UART (RX / TX) |
| D2 | Botão 1 da sequência (interrupção externa INT0) |
| D3, D4, D5 | Colunas do teclado 4x3 (`1 4 7 *`, `2 5 8 0`, `3 6 9 #`) |
| D6, D7, D8, D9 | Linhas do teclado 4x3 (`1 2 3`, `4 5 6`, `7 8 9`, `* 0 #`) |
| D10 | Botão 2 da sequência (interrupção de mudança de pino PCINT2) |
| D11 | Fio de cima amarelo e LED amarelo |
| D12 | Fio de cima verde e LED verde (também LED 2 da sequência e LED do cofre 2) |
| D13 | Fio de cima vermelho e LED vermelho (também LED 1 da sequência e LED do cofre 1) |
| A0 | Potenciômetro do cofre 1 |
| A1 | Potenciômetro do cofre 2 |
| A2 | Leitura dos fios de baixo (divisor de tensão) |
| A3 | Buzzer passivo |
| A4 / A5 | I2C (SDA / SCL): OLED SSD1306 em 0x3C e display HT16K33 em 0x70 |

### Teclado 4x3

Ligado na ordem dos terminais, do terminal 1 no D3 até o terminal 7 no D9. Nesse teclado, a bancada mostrou que D3 a D5 são as colunas e D6 a D9 são as linhas (o `teste_3_teclado` confirma a ordem de qualquer teclado). As linhas usam o pull-up interno do ATmega328P, então o teclado não precisa de resistores.

Como funciona a leitura: o código coloca uma coluna por vez em 0V e lê as 4 linhas. Uma linha em 0V indica a tecla que liga aquela linha à coluna acionada. O debounce é por tempo: o teclado é varrido a cada 5 ms e uma tecla só vale depois de ficar igual por 30 ms, ao apertar e ao soltar. Duas teclas juntas são ignoradas.

### Botões da sequência

Cada botão liga o pino (D2 ou D10) ao 5V, com resistor de 10k do pino para o GND (pull-down). Para usar o botão ligado ao GND com pull-up interno, mude `PULLDOWN_EXTERNO` para 0.

### Circuito dos fios

São 6 LEDs. Os 3 de cima ficam acesos durante o jogo, um por pino:

```
D13 ---[330R]---[+LED vermelho-]--- GND
D12 ---[330R]---[+LED verde-]------ GND
D11 ---[330R]---[+LED amarelo-]---- GND
```

Dos mesmos pinos saem os fios de cima. Embaixo, cada terminal tem um LED em série com um resistor, e os 3 caminhos terminam num ponto comum, ligado ao A2 e a 1k para o GND:

```
 terminal de baixo 1 ---[+LED-]--------------------+
 terminal de baixo 2 ---[+LED-]------[330R]--------+
 terminal de baixo 3 ---[+LED-]------[3300R]-------+---- A2
                                                   |
                                                 [1k]
                                                   |
                                                  GND
```

Cada caminho gera uma tensão diferente no A2, e o código descobre em qual terminal o fio está pela leitura do ADC. Para isso, mede um fio de cima por vez (5V) e deixa os outros em alta impedância. Os LEDs de baixo funcionam como diodos e impedem a corrente de voltar por outro fio.

Calibração medida na bancada, com os fios nas cores certas:

| Cor | Leitura no A2 | Faixa no código |
|---|---|---|
| Vermelho | ~143 | 100 a 349 |
| Verde | ~559 | 350 a 1023 |
| Amarelo | ~55 | 25 a 99 |
| Nada ligado | ~0 | abaixo de 25 |

As faixas ficam em `FIO_VERMELHO_MIN`, `FIO_VERMELHO_MAX` e assim por diante. Se o circuito mudar, grave o ambiente `jogo_fios`, ligue uma cor de cada vez e ajuste as faixas pelas leituras mostradas na serial.

### Buzzer

Precisa ser passivo, porque o Timer2 gera a frequência. A perna positiva vai no A3 e a outra no GND (com a polaridade invertida ele não toca).

## Como o software funciona

- Base de tempo: o Timer1 em modo CTC com prescaler 64 gera uma interrupção a cada 1 ms (`OCR1A = 249`). A ISR incrementa `ms` e, a cada 1000 ms, desconta 1 s do cronômetro. Todo o resto mede tempo comparando `ms`, sem `delay()`.
- ISRs curtas: as 5 interrupções (Timer1, Timer2, INT0, PCINT0 e recepção da UART) só atualizam variáveis `volatile` ou levantam flags. Quem decide o que fazer é o `loop()`.
- Laço principal sem bloqueio: o `loop()` lê o teclado, atualiza o cronômetro e chama um passo do desafio atual. Cada desafio é uma máquina de estados e devolve o controle a cada passo.
- Som: o Timer2 em modo CTC gera a frequência e a ISR inverte o pino A3. `bip()` liga o som e `atualizarBip()` desliga na hora certa, sem travar o jogo.
- Erros: todos os desafios chamam `registrarErro()`, que soma o erro e desconta a penalidade com as interrupções desligadas, porque a ISR também altera o cronômetro.
- Sorteio: a cada partida, a ordem parte da base e é embaralhada (Fisher-Yates). A semente mistura o tempo até o jogador escolher o modo com o contador do Timer1.

### Registradores por periférico

| Periférico | Registradores |
|---|---|
| GPIO | `DDRB/C/D`, `PORTB/C/D`, `PINB/D` |
| Timer1 | `TCCR1A/B`, `OCR1A`, `TCNT1`, `TIMSK1`, `TIFR1` |
| Timer2 | `TCCR2A/B`, `OCR2A`, `TCNT2`, `TIMSK2` |
| Interrupção externa | `EICRA`, `EIMSK`, `EIFR` (INT0); `PCICR`, `PCMSK0`, `PCIFR` (PCINT2) |
| ADC | `ADMUX`, `ADCSRA`, `ADC` |
| UART | `UBRR0H/L`, `UCSR0A/B/C`, `UDR0` |
| I2C | Pela biblioteca Wire |

### Bibliotecas

- `Wire` (Arduino): barramento I2C do OLED e do display de 7 segmentos.
- `SSD1306Ascii` (Bill Greiman, https://github.com/greiman/SSD1306Ascii): driver de texto do OLED. Foi escolhida porque escreve direto no display, sem guardar uma cópia da tela na RAM (o Uno tem só 2 KB).

O enunciado permite bibliotecas apenas para periféricos que a disciplina não cobre, como drivers de display.

### Estados da sequência

| Estado | O que faz |
|---|---|
| `SEQ_AGUARDA_INICIO` | Mostra `PRESSIONE BOTAO` e espera qualquer botão |
| `SEQ_INICIO_SOLTAR` | Espera soltar o botão e começa a rodada |
| `SEQ_APRESENTANDO` | Pisca os 2 LEDs juntos e depois a sequência, 300 ms aceso e 300 ms apagado |
| `SEQ_PAUSA` | Pausa de 500 ms antes da resposta |
| `SEQ_ESPERA_BOTAO` | Espera a flag `botao_evento` das interrupções |
| `SEQ_CONFIRMA_BOTAO` | Depois de 30 ms, lê os botões de novo e acende o LED do botão apertado |
| `SEQ_SOLTAR` | Espera soltar os botões |
| `SEQ_SOLTAR_REPIQUE` | 30 ms de repique na liberação, LED aceso por no mínimo 150 ms, depois confere a resposta |
| `SEQ_ERRO` | 3 piscadas e pausa de 1 s, depois volta para `SEQ_AGUARDA_INICIO` |
| `SEQ_VITORIA` | LEDs acesos 1 s, apagados 300 ms e acesos de novo |

## Mudanças em relação aos códigos originais

- Timer1: cada módulo tinha a própria ISR (1 s, 10 ms ou 1 ms). Agora existe uma ISR de 1 ms, compartilhada.
- Timer2: continua gerando o som, mas o pino passou a ser invertido pela ISR `TIMER2_COMPA_vect`, porque a saída de hardware (D11) agora é um fio.
- UART: o `Serial` da bomba e dos fios foi trocado pelas funções por registradores da sequência, e a UART do cofre foi unificada com elas. O `R` reinicia o jogo inteiro.
- Display: o LCD 16x2 com PCF8574 foi trocado por um OLED 0,96" I2C. As funções do LCD (`limparLCD`, `linhaLCD`, `textoLCD`, `enviarLCD`, `comandoLCD`) mantiveram o nome e agora desenham no OLED. O desafio hexadecimal, que escrevia direto no PCF8574, passou a usar essas mesmas funções.
- Teclado: o 4x4 da bomba virou um 4x3, com pull-up interno e debounce por tempo.
- Cada `setup()` e `loop()` de desafio virou `xxx_iniciar()` e `xxx_executar()`. O `executar` faz um passo, volta sem bloquear e devolve 1 quando o desafio termina.
- Erros: os desafios chamam `registrarErro()` em vez de contar sozinhos.
- Fios: o contador de 5 s e o `while(1)` final saíram. O desafio termina quando os 3 fios estão certos, e o circuito ganhou LEDs nos terminais de baixo, com as faixas do ADC recalibradas.
- Sequência: virou máquina de estados, os botões passaram a usar interrupção (INT0 e PCINT2), a rodada começa pelo botão e o LED acende a cada clique.
- Funções do Arduino: `delay()` e `delayMicroseconds()` viraram `_delay_ms()` e `_delay_us()` da avr-libc. O jogo não usa mais nenhuma função pronta do Arduino para periféricos.

## Nomes alterados em relação aos códigos originais

Nomes que não aparecem nas tabelas abaixo continuam iguais aos originais.

### Comuns a todos os módulos

| Original | No código integrado | Motivo |
|---|---|---|
| `setup()` e `loop()` de cada desafio | `xxx_iniciar()` e `xxx_executar()` | Só pode existir um `setup()` e um `loop()` (os da bomba) |
| `ISR(TIMER1_COMPA_vect)` de cada módulo | Uma única ISR no módulo da bomba | Só pode existir uma ISR por vetor |
| `set_bit`, `clr_bit`, `cpl_bit`, `tst_bit` repetidos em cada arquivo | Definidos uma vez no topo | Evitar repetição |

### bomba.ino

| Original | No código integrado |
|---|---|
| `Serial.begin(9600)` | `uart_init()` |
| `Serial.println(texto)` | `uart_enviar_linha(texto)` |
| `Serial.print(texto)` | `uart_enviar_texto(texto)` |
| `Serial.println(tecla)` | `uart_enviar_char(tecla)` seguido de `uart_enviar_texto("\r\n")` |
| `delay(x)` | `_delay_ms(x)` |
| `delayMicroseconds(x)` | `_delay_us(x)` |
| `LCD_ADDR 0x20` | `OLED_ADDR 0x3C` |
| `enviarPCF()`, `pulsoLCD()`, `nibbleLCD()` | Removidas. `comandoLCD()` e `enviarLCD()` desenham no OLED |
| Máscara `0b00111100` das linhas do teclado | `COLUNAS_TECLADO` (`0b00111000`: colunas D3 a D5) |
| `colunaPressionada()` | `linhaPressionada()` (agora as linhas é que são lidas) |
| `teclas[4][4]` (teclado 4x4) | `teclas[4][3]` (teclado 4x3) |
| `lerTeclado()` com contador de 4 leituras | `lerTeclado()` com debounce por tempo |
| `#` na tela de vitória para jogar de novo | Segurar `*` por 1 s em qualquer momento (`asteriscoSegurado()`) |
| `180` (tempo inicial em `iniciarJogo`) | `TEMPO_TOTAL_S` |
| Trecho do `loop()` que lia o cronômetro e atualizava o display | `servicoBomba()` |
| Fim de jogo dentro do `loop()` (`jogoEncerrado = 1`) | `encerrarJogo(venceu)` e `reiniciarJogo()` |

Novos nomes na bomba: `oled`, `posicaoOLED()`, `OLED_LARGURA_CARACTERE`, `OLED_PAGINA_LINHA0`, `OLED_PAGINA_LINHA1`, `mostrarTempoLCD()`, `tempoLCDPendente`, `COLUNA_TEMPO_LCD`, `teclaPressionada`, `inicioTeclaPressionada`, `INTERVALO_VARREDURA_MS`, `DEBOUNCE_TECLADO_MS`, `asteriscoSegurado()`, `TEMPO_REINICIO_MS`, `registrarErro()`, `bip()`, `atualizarBip()`, `telaExplosao()`, `telaVitoria()`, `telaDesafio()`, `sortearOrdem()`, `sortearIndice()`, `desafioUltimaPartida`, `iniciarDesafioAtual()`, `executarDesafioAtual()`, as variáveis `erros`, `ordemDesafios`, `indiceDesafio`, `desafioIniciado`, `vitoria`, `esperarAte`, `momentoFim`, `teclaJogo`, `contadorSegundo` e as constantes `PENALIDADE_FACIL_S`, `PENALIDADE_DIFICIL_S`, `MAX_ERROS_JOGO`, `TEMPO_AVISO_S`, `PAUSA_DESAFIOS_MS`, `TEMPO_EXPLOSAO_MS`, `NUM_DESAFIOS`, `TOTAL_DESAFIOS`, `DESAFIO_HEXADECIMAL`, `DESAFIO_COFRE`, `DESAFIO_SEQUENCIA` e `DESAFIO_FIOS`.

### binario_decimal1 (2).ino (desafio hexadecimal)

No código integrado, este desafio se chama hexadecimal (o arquivo original manteve o nome).

| Original | No código integrado |
|---|---|
| `setup()` | `hexadecimal_iniciar()` |
| `loop()` | `hexadecimal_executar()` |
| `static uint8_t erros` (erros do desafio) | `erros` global da bomba (erros do jogo todo) |
| `MAX_ERROS` | `MAX_ERROS_JOGO` (na bomba) |
| `lcd_byte()` escrevendo no PCF8574 | `lcd_byte()` chamando `enviarLCD()` (OLED) |
| `teclado_tecla()` lendo o teclado próprio | `teclado_tecla()` devolvendo `teclaJogo` (teclado da bomba) |
| Semente fixa `fase_teclado_inicia(1234)` | Semente vinda do tempo: `ler_tempo_ms() ^ TCNT1` |

Removidos: `CLOCK_HZ`, `TWI_SCL_HZ`, `PCF_ENDERECO`, `LCD_E`, `LCD_LUZ`, `twi_init()`, `twi_espera()`, `twi_start()`, `twi_stop()`, `twi_envia()`, `pcf_escreve_dois()`, `lcd_nibble()`, `lcd_init()`, `LINHAS_DDR`, `LINHAS_PORT`, `COLUNAS_DDR`, `COLUNAS_PORT`, `COLUNAS_PIN`, `BIT_LINHA`, `BIT_COLUNA`, `MAPA_TECLAS`, `teclado_init()`, `teclado_varre()`, `tick_10ms`, `fase_teclado_erros()`, `DESAFIO_FALHOU` e `FASE_FALHOU`.

### cofre.ino

| Original | No código integrado |
|---|---|
| `setup()` | `cofre_iniciar()` |
| `loop()` | `cofre_executar()` |
| `desafio_concluido` | `cofre_concluido` (a sequência tinha uma variável com o mesmo nome) |
| `tempo_ms` (16 bits) | `ms` compartilhado (32 bits), lido por `agora_ms()` |
| `LED_COFRE1 PD5` | `LED_COFRE1 PB5` (D13) |
| `LED_COFRE2 PD6` | `LED_COFRE2 PB4` (D12) |
| `PORTD` e `DDRD` dos LEDs | `LED_COFRE_PORT` e `LED_COFRE_DDR` |
| `uart_init()` só de transmissão | `uart_init()` da sequência (transmissão e recepção) |

### desafio_sequencia.ino

| Original | No código integrado |
|---|---|
| `setup()` | `sequencia_iniciar()` |
| `loop()` | `sequencia_executar()` |
| `desafio_concluido` | `sequencia_concluida` (o cofre tinha uma variável com o mesmo nome) |
| `ms` (8 bits) | `ms` compartilhado (32 bits) |
| `LED1 PD6` | `LED1 PB5` (D13) |
| `LED2 PD4` | `LED2 PB4` (D12) |
| `BOTAO2 PD3` | `BOTAO2 PB2` (D10) |
| `PORTD`, `DDRD` e `PIND` dos LEDs e botões | `LED_PORT`, `LED_DDR`, `BOTAO1_DDR`, `BOTAO1_PORT`, `BOTAO1_PIN`, `BOTAO2_DDR`, `BOTAO2_PORT`, `BOTAO2_PIN` |
| `PRESSIONADO(pino)` lendo sempre `PIND` | `PRESSIONADO(pino)` lendo o `PIN` da porta de cada botão |
| `uart_enviar_numero(uint8_t)` | `uart_enviar_numero(uint16_t)` do cofre |
| `espera_ms()`, `apresentar_sequencia()`, `aguardar_soltar()`, `ler_botao()`, `indicar_erro()`, `indicar_vitoria()` | Estados `SEQ_*` (ver tabela de estados) |

Novos nomes na sequência: `botao_evento`, `ISR(INT0_vect)`, `ISR(PCINT0_vect)`, `seq_estado`, `seq_passo`, `seq_posicao`, `seq_resposta`, `seq_marca`, `seq_marca_led`, `seq_ir_para()`, `seq_decorrido()`, `seq_proximo_passo()`, `seq_aguardar_inicio()`, `seq_conferir_resposta()` e as constantes de tempo `SEQ_TEMPO_LED`, `SEQ_PAUSA_RESPOSTA`, `SEQ_DEBOUNCE`, `SEQ_PISCA_ERRO`, `SEQ_PAUSA_ERRO`, `SEQ_VITORIA_ACESO`, `SEQ_VITORIA_APAGADO` e `SEQ_PISCA_CLIQUE`.

`reiniciar_solicitado`, `COMANDO_REINICIO` e `ISR(USART_RX_vect)` continuam com o mesmo nome, mas foram para o módulo compartilhado porque agora reiniciam o jogo inteiro.

### fios.ino

| Original | No código integrado |
|---|---|
| `setup()` | `fios_iniciar()` |
| `loop()` | `fios_executar()` |
| `finalizar_jogo()` | `finalizar_fios()` (não encerra mais o jogo, só o desafio) |
| `penalidade_ms += 5000UL` em `aplicar_regra()` | `registrarErro()` |
| Leitura em `PC1` / `MUX0` (A1) | `PC2` / `MUX1` (A2) |
| Faixas fixas em `identificar_fio()` (200-230, 95-125, 170-195) | Constantes `FIO_VERMELHO_MIN/MAX`, `FIO_VERDE_MIN/MAX`, `FIO_AMARELO_MIN/MAX`, recalibradas |
| `testar_fio()` com os outros fios em 0V | `testar_fio()` com os outros fios em alta impedância |
| `Serial.print(...)` | `uart_enviar_texto()` e `uart_enviar_numero()` |

Removidos: `endereco`, `numeros`, `inicio`, `penalidade_ms`, `ultimo`, `encerrado`, `configurar_timer()`, `configurar_display()`, `comando()`, `mostrar()` e `mostrar_resultado()`. `ms` e `ler_tempo_ms()` foram para o módulo compartilhado.

Novos nomes nos fios: `fios_concluido`, `fios_todos_corretos()`, `FIOS_TEMPO_ESTAVEL_MS` e `fios_ultimo_valor` (relatório do modo de teste).

### buzzer.ino

| Original | No código integrado |
|---|---|
| Pino `PB3` (D11, saída OC2A) | `BUZZER_PORT`, `BUZZER_DDR`, `BUZZER_BIT` (`PC3`, A3) |
| `COM2A0` em `meu_tone()` (hardware inverte o pino) | `ISR(TIMER2_COMPA_vect)` inverte o pino |
| `setup()` e `loop()` de teste | Removidos, o buzzer é usado por `bip()` |

`meu_tone()` e `meu_noTone()` mantêm o nome.

## Validação

O jogo completo e cada desafio foram testados no Arduino Uno com o hardware final (OLED, teclado 4x3, display de 7 segmentos, potenciômetros, botões, circuito dos fios com 6 LEDs e buzzer), usando os testes de bancada e os ambientes `jogo_*`. O código compila sem avisos: cerca de 12 KB de flash e 1,2 KB de RAM.

## Uso de inteligência artificial

O projeto usou o Claude Code (modelo Claude Opus 5.5, da Anthropic). Todos os prompts estão em `Registro_IA_Escape_Room.pdf`.

## Pendências em relação ao enunciado

- Código final: o enunciado pede que o jogador digite um código final. O número sorteado pelos fios pode ser usado para isso, mas essa etapa ainda não existe.
