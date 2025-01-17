# tictactoe-bitdoglab

Desenvolvi esse jogo para testar os diversos recursos da BigDogLab e também do processador Raspberry Pi Pico W RP2040. É uma versão simpleficada e minimalista do jogo da velha.

## Como jogar?

Use o joystic para mover o cursor (led piscando) entre as células do jogo. O botão A é o player 1 e o botão B o player 2. No display é possível ver a pontuação de cada jogador.

## Recursos

- Leitura analógica (ADC) para ler o Joystic.
- Entrada digital para leitura dos botões.
- Saída PWM para tocar o buzzer.
- Cominucação i2c e recurso PIO para controlar os led endereçaveis ws2812.
- Comunicação i2c para o display SSD1306.
- Timer e alarm para interrupções baseadas em tempo.
- Uso de multicore para o buzzer não 'travar' os leds enquanto toca.
