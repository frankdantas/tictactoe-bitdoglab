#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h" //Para usar o PIO para a matriz de led
// #include "hardware/clocks.h"//Para usar o relogio
#include "hardware/adc.h"    //Para usar o ADC de ler o joystic
#include "hardware/timer.h"  //Para usar o timer/alarm
#include "hardware/pwm.h"    //Para usar o PWM para o buzzer
#include "ssd1306/ssd1306.h" //Para usar o display
#include "hardware/i2c.h"    //Para usar o I2C para a matriz de led
#include "pico/multicore.h"  //Para usar o multicore para tocar o buzzer (para não 'travar' o main principal)
#include "ws2812.pio.h"      //Para usar o PIO para a matriz de led

#define PIN_JOYSTICK_Y 0
#define PIN_JOYSTICK_X 1
#define PIN_BTN_A 5
#define PIN_BTN_B 6
#define PIN_BUZZER_A 10
#define PIN_BUZZER_B 28

#define MATRIX_WIDTH 5
#define MATRIX_HEIGHT 5
#define MAX_FREE_CELLS 9 // Quantidade maxima de celulas possiveis para preencher o tabuleiro

#define ADC_PIN_X (26 + PIN_JOYSTICK_X)
#define ADC_PIN_Y (26 + PIN_JOYSTICK_Y)
#define ADC_RANGE (1 << 12)

// Identificadores para as cores e células
#define INDEX_FUNDO 0
#define INDEX_BRANCO 1
#define INDEX_CURSOR 2
#define INDEX_PLAYER_A 3
#define INDEX_PLAYER_B 4

#define IS_RGBW false
#define NUM_PIXELS 25
#define WS2812_PIN 7

static inline void put_pixel(PIO pio, uint sm, uint32_t pixel_grb)
{
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)(r) << 8) |
           ((uint32_t)(g) << 16) |
           (uint32_t)(b);
}

static inline uint32_t urgbw_u32(uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    return ((uint32_t)(r) << 8) |
           ((uint32_t)(g) << 16) |
           ((uint32_t)(w) << 24) |
           (uint32_t)(b);
}

// Estrutura para armazenar a posicao xy do cursor
typedef struct
{
    int8_t x;
    int8_t y;
} cursor_t;

// Status do jogo
enum STATUS_GAME
{
    STATUS_PLAYING,
    STATUS_WINNER,
    STATUS_DRAW,
    STATUS_RESTART
};

// Array oficial usado para validar vencedor ou empate
volatile uint8_t tabuleiro[MATRIX_WIDTH][MATRIX_HEIGHT] = {
    {INDEX_FUNDO, INDEX_BRANCO, INDEX_FUNDO, INDEX_BRANCO, INDEX_FUNDO},
    {INDEX_BRANCO, INDEX_BRANCO, INDEX_BRANCO, INDEX_BRANCO, INDEX_BRANCO},
    {INDEX_FUNDO, INDEX_BRANCO, INDEX_FUNDO, INDEX_BRANCO, INDEX_FUNDO},
    {INDEX_BRANCO, INDEX_BRANCO, INDEX_BRANCO, INDEX_BRANCO, INDEX_BRANCO},
    {INDEX_FUNDO, INDEX_BRANCO, INDEX_FUNDO, INDEX_BRANCO, INDEX_FUNDO}};

// Array 'virtual' usando para pintar a matriz de led
volatile uint8_t tabuleiro_virtual[MATRIX_WIDTH][MATRIX_HEIGHT] = {
    {INDEX_FUNDO, INDEX_BRANCO, INDEX_FUNDO, INDEX_BRANCO, INDEX_FUNDO},
    {INDEX_BRANCO, INDEX_BRANCO, INDEX_BRANCO, INDEX_BRANCO, INDEX_BRANCO},
    {INDEX_FUNDO, INDEX_BRANCO, INDEX_FUNDO, INDEX_BRANCO, INDEX_FUNDO},
    {INDEX_BRANCO, INDEX_BRANCO, INDEX_BRANCO, INDEX_BRANCO, INDEX_BRANCO},
    {INDEX_FUNDO, INDEX_BRANCO, INDEX_FUNDO, INDEX_BRANCO, INDEX_FUNDO}};

volatile bool canDrawCursor = true;                    // Indica se pode desenhar o cursor na célula
volatile enum STATUS_GAME gameStatus = STATUS_PLAYING; // Indica o status do jogo
volatile cursor_t celulasWin[3];                       // Celulas vencedoras
volatile uint8_t count_pressed_keys = 0;               // Contador de teclas pressionadas
volatile uint8_t winner = 0;                           // Indice do jogador vencedor
volatile bool doneSound = false;                       // Indica se o som foi tocado
volatile uint8_t score_player_A = 0;                   // Pontuação do jogador A
volatile uint8_t score_player_B = 0;                   // Pontuação do jogador B

bool invertBlinkColor = false;      // Flag para ficar invertendo e indicar qual a cor da celula
cursor_t cursor = {.x = 0, .y = 0}; // Posição do cursor
int last_step_x = 0;                // Ultima mudança no eixo x
int last_step_y = 0;                // Ultima mudança no eixo y

uint32_t cores[5]; // Array de cores para pintar a matriz de led

PIO pio; // Objeto PIO para a matriz de led
uint sm; // Objeto SM stateMachine para a matriz de led

ssd1306_t disp; // Objeto para o display SSD1306

// Funcao para configurar o display
void setup_display()
{
    i2c_init(i2c1, 400000);
    gpio_set_function(14, GPIO_FUNC_I2C);
    gpio_set_function(15, GPIO_FUNC_I2C);
    gpio_pull_up(14);
    gpio_pull_up(15);

    disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
    ssd1306_clear(&disp);
}

// Funcao para mostrar uma mensagem
void mostrar_mensagem(char *str, uint32_t x, uint32_t y, bool should_clear)
{
    if (should_clear)
    {
        ssd1306_clear(&disp);
    }
    sleep_ms(50);
    ssd1306_draw_string(&disp, x, y, 1, str);
    ssd1306_show(&disp);
}

// Reinicia o jogo
void reset()
{
    count_pressed_keys = 0;
    gameStatus = STATUS_PLAYING;
    winner = 0;
    doneSound = false;
    last_step_x = 0;
    last_step_y = 0;
    cursor.x = 0;
    cursor.y = 0;
    canDrawCursor = true;

    for (size_t i = 0; i < MATRIX_HEIGHT; i += 2)
    {
        for (size_t j = 0; j < MATRIX_WIDTH; j += 2)
        {
            tabuleiro[i][j] = INDEX_FUNDO;
            tabuleiro_virtual[i][j] = INDEX_FUNDO;
        }
    }

    mostrar_mensagem("Playing", 5, 2, true);
    char frase[50];
    sprintf(frase, "A: %d vs. B: %d", score_player_A, score_player_B);
    mostrar_mensagem(frase, 5, 30, false);
}

// Função para tocar uma nota no buzzer
void tocar_nota_buzzer(uint gpio, uint32_t frequencia, uint32_t duracao_ms)
{
    uint32_t clock_freq = 125000000;

    gpio_set_function(gpio, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_set_clkdiv(slice_num, (clock_freq / frequencia)); // Divisor de clock
    pwm_set_wrap(slice_num, 1000);
    pwm_set_gpio_level(gpio, 100);
    pwm_set_enabled(slice_num, true);
    sleep_ms(duracao_ms);
    pwm_set_enabled(slice_num, false);
    sleep_ms(10);
}

void tocar_sequencia_notas(uint32_t* notas, uint32_t* tempos, size_t tamanho) {
    for (size_t i = 0; i < tamanho; i++) {
        tocar_nota_buzzer(PIN_BUZZER_A, notas[i], tempos[i]);
    }
}

// Função para tocar o som de vitoria
void tocar_som_ganhou()
{
    printf("Tocar som ganhouz\n");
    uint32_t notas[] = {523, 587, 659, 698, 784, 880, 987, 1046}; // Frequências das notas (C, D, E, F, G, A, B, C')
    uint32_t tempos[] = {100, 100, 100, 100, 100, 100, 100, 100}; // Durabilidade das notas em milissegundos (100ms cada)

    tocar_sequencia_notas(notas, tempos, 8);
}

// Função para tocar o som de empate
void tocar_som_empate()
{

    printf("Tocar som empate\n");
    uint32_t notas[] = {196, 164, 131, 110, 98, 82};    // Frequências das notas (G, E, C, F, D, A)
    uint32_t tempos[] = {250, 250, 300, 300, 400, 400}; // Durabilidade das notas em milissegundos

    tocar_sequencia_notas(notas, tempos, 6);
}

// Função para inicializar as cores
void init_cores()
{
    uint8_t intensidadeCor = 5;
    cores[INDEX_FUNDO] = urgb_u32(0, 0, 0);                            // Desligado
    cores[INDEX_BRANCO] = urgb_u32(1, 1, 1);                           // Branco
    cores[INDEX_CURSOR] = urgb_u32(intensidadeCor, intensidadeCor, 0); // Amarelo
    cores[INDEX_PLAYER_A] = urgb_u32(intensidadeCor, 0, 0);            // Vermelho
    cores[INDEX_PLAYER_B] = urgb_u32(0, 0, intensidadeCor);            // Azul
}

// Função para mostrar o tabuleiro
void showTabuleiro(PIO pio, uint sm)
{
    uint32_t intensidadeCor = 5;

    for (int linha = MATRIX_HEIGHT - 1; linha >= 0; linha--)
    {
        if (linha % 2 == 0)
        { // Se o index da linha for par lê as colunas de trás para frente
            for (int coluna = MATRIX_WIDTH - 1; coluna >= 0; coluna--)
            {
                put_pixel(pio, sm, cores[tabuleiro_virtual[linha][coluna]]);
            }
        }
        else
        { // Se o index da linha for impar lê as colunas de frente para trás
            for (int coluna = 0; coluna < MATRIX_WIDTH; coluna++)
            {
                put_pixel(pio, sm, cores[tabuleiro_virtual[linha][coluna]]);
            }
        }
    }

    sleep_us(100);
}

// Função para apagar o tabuleiro
void pattern_clear_all(PIO pio, uint sm, uint len, uint t)
{
    printf("Rodando pattern_clear_all\n");
    // Itera por todos os LEDs e apaga-os, enviando 0x000000 (preto)
    for (uint i = 0; i < len; ++i)
    {
        put_pixel(pio, sm, 0x000000); // Apaga o LED
    }
}

// Função para mapear uma faixa de valores para outra
long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Função para ler o movimento X do joystic
/*bool  read_move_cursor_x(){
    adc_select_input(PIN_JOYSTICK_X);
    sleep_us(2);
    uint32_t adc_raw = adc_read();
    int step = map(adc_raw, 0, ADC_RANGE, -10, 10);// Mapeia o valor lido pelo ADC para um valor entre -10 e 10. Se estiver mais de 80% para um lado, então considera o valor
    if(step < -8){
        step = -1;
    }else if(step > 8){
        step = 1;
    }else{
        step = 0;
    }

    // Se não houve mudança no valor, sai fora
    if(last_step_x == step){
        return false;
    }
    last_step_x = step;
    cursor.x += step;
    if(cursor.x < 0) cursor.x = 0;
    if(cursor.x > 2) cursor.x = 2; // 2 é a quantidade de index maximo onde o curso pode estar, 0, 2, 4 (ou seja, um array de 3 posições, a maxima é a 2)
    return true;
}

// Função para ler o movimento Y do joystic
bool read_move_cursor_y(){
    adc_select_input(PIN_JOYSTICK_Y);
    sleep_us(2);
    uint32_t adc_raw = adc_read();
    int step = map(adc_raw, 0, ADC_RANGE, 10, -10);// Mapeia o valor lido pelo ADC para um valor entre -10 e 10. Se estiver mais de 80% para um lado, então considera o valor
    if(step < -8){
        step = -1;
    }else if(step > 8){
        step = 1;
    }else{
        step = 0;
    }

    // Se não houve mudança no valor, sai fora
    if(last_step_y == step){
        return false;
    }
    last_step_y = step;
    cursor.y += step;
    if(cursor.y < 0) cursor.y = 0;
    if(cursor.y > 2) cursor.y = 2; // 2 é a quantidade de index maximo onde o curso pode estar, 0, 2, 4 (ou seja, um array de 3 posições, a maxima é a 2)
    return true;
}*/

bool read_move_cursor(int pin_adc, int *last_step, int8_t *cursor_pos, int8_t max_index, int minRange, int maxRange)
{
    adc_select_input(pin_adc);
    sleep_us(2);
    uint32_t adc_raw = adc_read();
    int step = map(adc_raw, 0, ADC_RANGE, minRange, maxRange);
    step = (step < -8) ? -1 : (step > 8) ? 1
                                         : 0;

    if (*last_step == step)
        return false;

    *last_step = step;
    *cursor_pos += step;
    if (*cursor_pos < 0)
        *cursor_pos = 0;
    if (*cursor_pos > max_index)
        *cursor_pos = max_index;

    return true;
}

// Função para ler o movimento X e Y do joystic e depois desenhar na matriz
void on_ler_e_desenha()
{
    if (canDrawCursor && (gameStatus == STATUS_PLAYING))
    {
        cursor_t prevPos = cursor; // Salva a posição atual do cursor

        // O 2, nas funções abaixo é o maxIndex que pode ser alcancado, como só tem 3 celulas preenchiveis em cada direção, entao o index maximo e 2 (0, 1, 2)
        bool moveuX = read_move_cursor(PIN_JOYSTICK_X, &last_step_x, &cursor.x, 2, -10, 10); // || read_move_cursor_x();
        bool moveuY = read_move_cursor(PIN_JOYSTICK_Y, &last_step_y, &cursor.y, 2, 10, -10); // || read_move_cursor_y();

        bool hasChangePos = moveuX || moveuY;
        if (hasChangePos)
        { // Se teve mudança no curso, a célula antiga, recebe o valor 'oficial'
            tabuleiro_virtual[prevPos.y * 2][prevPos.x * 2] = tabuleiro[prevPos.y * 2][prevPos.x * 2];
        }
    }
    showTabuleiro(pio, sm);
}

void tocar_som_final()
{
    if (doneSound)
    {
        return;
    }

    sleep_ms(50);
    if (gameStatus == STATUS_WINNER)
    {
        printf("Ganhou!\n");
        tocar_som_ganhou();
        if (winner == INDEX_PLAYER_A)
        {
            mostrar_mensagem("Player A", 5, 2, true);
            mostrar_mensagem("Won", 5, 20, false);
        }
        else if (winner == INDEX_PLAYER_B)
        {
            mostrar_mensagem("Player B", 5, 2, true);
            mostrar_mensagem("Won", 5, 20, false);
        }
        doneSound = true;
    }
    else if (gameStatus == STATUS_DRAW)
    {
        printf("Empate!\n");
        tocar_som_empate();
        mostrar_mensagem("Empate", 5, 2, true);
        doneSound = true;
    }
}
// Callback para o alarme de reset
int64_t reset_game(alarm_id_t id, void *user_data)
{
    printf("Reset.\n");
    gameStatus = STATUS_RESTART;
    return 0;
}

// Verifica se houve vencedor
void check_winner()
{
    bool hadWinner = false;
    int lineWinner = -1;
    int collumnWinner = -1;

    for (size_t i = 0; i < MATRIX_HEIGHT && !hadWinner; i += 2)
    {
        if (tabuleiro[0][i] == tabuleiro[2][i] && tabuleiro[0][i] == tabuleiro[4][i] && tabuleiro[0][i] != INDEX_FUNDO)
        {
            collumnWinner = i;
            celulasWin[0].x = 0;
            celulasWin[0].y = i;
            celulasWin[1].x = 2;
            celulasWin[1].y = i;
            celulasWin[2].x = 4;
            celulasWin[2].y = i;
            hadWinner = true;
            winner = tabuleiro[0][i];
        }
    }

    for (size_t i = 0; i < MATRIX_WIDTH && !hadWinner; i += 2)
    {
        if (tabuleiro[i][0] == tabuleiro[i][2] && tabuleiro[i][0] == tabuleiro[i][4] && tabuleiro[i][0] != INDEX_FUNDO)
        {
            lineWinner = i;
            celulasWin[0].x = i;
            celulasWin[0].y = 0;
            celulasWin[1].x = i;
            celulasWin[1].y = 2;
            celulasWin[2].x = i;
            celulasWin[2].y = 4;
            winner = tabuleiro[i][0];
            hadWinner = true;
        }
    }

    if (!hadWinner)
    { // Verificar diagonal
        if (tabuleiro[0][0] == tabuleiro[2][2] && tabuleiro[0][0] == tabuleiro[4][4] && tabuleiro[0][0] != INDEX_FUNDO)
        {
            celulasWin[0].x = 0;
            celulasWin[0].y = 0;
            celulasWin[1].x = 2;
            celulasWin[1].y = 2;
            celulasWin[2].x = 4;
            celulasWin[2].y = 4;
            hadWinner = true;
            winner = tabuleiro[0][0];
        }

        if (tabuleiro[0][4] == tabuleiro[2][2] && tabuleiro[0][4] == tabuleiro[4][0] && tabuleiro[0][4] != INDEX_FUNDO)
        {
            celulasWin[0].x = 0;
            celulasWin[0].y = 4;
            celulasWin[1].x = 2;
            celulasWin[1].y = 2;
            celulasWin[2].x = 4;
            celulasWin[2].y = 0;
            hadWinner = true;
            winner = tabuleiro[0][4];
        }
    }

    if (hadWinner)
    {
        printf("Houve um ganhador na linha %d e na coluna %d\n", lineWinner, collumnWinner);
        gameStatus = STATUS_WINNER;
        if(winner == INDEX_PLAYER_A){
            score_player_A++;
        }else if(winner == INDEX_PLAYER_B){
            score_player_B++;
        }

        add_alarm_in_ms(5000, reset_game, NULL, false);
    }
    else
    {
        // Verificar se deu empate
        /*bool temCelulaVazia = false;//Considero que preenchi tudo
        for (size_t i = 0; i < MATRIX_HEIGHT && !temCelulaVazia; i+=2)
        {
            for (size_t j = 0; j < MATRIX_WIDTH && !temCelulaVazia; j+=2)
            {
                if(tabuleiro[i][j] == INDEX_FUNDO){
                    temCelulaVazia = true;
                }
            }
        }*/
        // if(!temCelulaVazia){
        //  Se pressionei a quantidade de celulas livre e nao ganhou, entao deu empate
        if (count_pressed_keys == MAX_FREE_CELLS)
        {
            gameStatus = STATUS_DRAW;
            add_alarm_in_ms(5000, reset_game, NULL, false);
        }
    }

    // print todo o tabuleiro
    for (size_t i = 0; i < MATRIX_HEIGHT; i++)
    {
        for (size_t j = 0; j < MATRIX_WIDTH; j++)

        {
            printf("%d\t", tabuleiro[i][j]);
        }
        printf("\n");
    }
}

// Ativa o cursor de novo
int64_t enable_draw_cursor(alarm_id_t id, void *user_data)
{
    printf("Ativou cursor.\n");
    canDrawCursor = true;
    return 0;
}

// Verificação dos botões
bool checkbutton_timer(struct repeating_timer *t)
{
    static absolute_time_t last_press_time_A = 0;
    static bool button_last_state_A = false;
    static absolute_time_t last_press_time_B = 0;
    static bool button_last_state_B = false;

    bool button_pressed_A = !gpio_get(PIN_BTN_A); // Pressionado = LOW
    bool button_pressed_B = !gpio_get(PIN_BTN_B); // Pressionado = LOW

    if (button_pressed_A && !button_last_state_A && absolute_time_diff_us(last_press_time_A, get_absolute_time()) > 200000)
    { // 200 ms
        last_press_time_A = get_absolute_time();
        button_last_state_A = true;
        // canDrawCursor = false;
        // add_alarm_in_ms(500, enable_draw_cursor, NULL, false);
        printf("Apertei A. A celula tem valor: %d\n", tabuleiro[cursor.y * 2][cursor.x * 2]);

        if (tabuleiro[cursor.y * 2][cursor.x * 2] == INDEX_FUNDO)
        {
            tabuleiro[cursor.y * 2][cursor.x * 2] = INDEX_PLAYER_A;
            printf("Pintou a celula\n");
            count_pressed_keys++;
            check_winner();
            canDrawCursor = false;
            add_alarm_in_ms(250, enable_draw_cursor, NULL, false);
        }
        else
        {
            printf("Celula ja preenchida\n");
        }
    }
    else if (!button_pressed_A)
    {
        button_last_state_A = false;
    }

    if (button_pressed_B && !button_last_state_B && absolute_time_diff_us(last_press_time_B, get_absolute_time()) > 200000)
    { // 200 ms
        last_press_time_B = get_absolute_time();
        button_last_state_B = true;

        printf("Apertei B. A celula tem valor: %d\n", tabuleiro[cursor.y * 2][cursor.x * 2]);

        if (tabuleiro[cursor.y * 2][cursor.x * 2] == INDEX_FUNDO)
        {
            tabuleiro[cursor.y * 2][cursor.x * 2] = INDEX_PLAYER_B;
            printf("Pintou a celula\n");
            count_pressed_keys++;
            check_winner();
            canDrawCursor = false;
            add_alarm_in_ms(250, enable_draw_cursor, NULL, false);
        }
        else
        {
            printf("Celula ja preenchida\n");
        }
    }
    else if (!button_pressed_B)
    {
        button_last_state_B = false;
    }
    return true; // Continuar o temporizador de repetição
}

// Faz piscar o cursor ou a linha vencedora
bool blink_cursor_timer(struct repeating_timer *t)
{
    if (canDrawCursor)
    {
        invertBlinkColor = !invertBlinkColor;

        if (gameStatus == STATUS_PLAYING)
        {
            tabuleiro_virtual[cursor.y * 2][cursor.x * 2] = invertBlinkColor ? tabuleiro[cursor.y * 2][cursor.x * 2] : INDEX_CURSOR;
        }
        else if (gameStatus == STATUS_WINNER)
        {
            for (size_t i = 0; i < 3; i++)
            {
                tabuleiro_virtual[celulasWin[i].x][celulasWin[i].y] = invertBlinkColor ? tabuleiro[cursor.y * 2][cursor.x * 2] : INDEX_FUNDO;
            }
        }
        else
        { // Empate
            for (size_t i = 0; i < MATRIX_HEIGHT; i += 2)
            {
                for (size_t j = 0; j < MATRIX_WIDTH; j += 2)
                {
                    tabuleiro_virtual[i][j] = invertBlinkColor ? tabuleiro[i][j] : INDEX_FUNDO;
                }
            }
        }
    }
    return true;
}

void tarefa_no_nucleo1()
{
    while (1)
    {
        tocar_som_final();
        sleep_ms(10);
    }
}

int main()
{
    stdio_init_all();

    init_cores();

    /*printf("Esperando abrir serial monitor...");
    while (!stdio_usb_connected()) {
      printf(".");
      sleep_ms(500);
    }
    printf("\nSerial aberta!\n");*/
    printf("Matriz de led no pino %d\n", WS2812_PIN);

    adc_init();

    adc_gpio_init(ADC_PIN_X);
    adc_gpio_init(ADC_PIN_Y);

    gpio_init(PIN_BTN_A);
    gpio_init(PIN_BTN_B);
    gpio_set_dir(PIN_BTN_A, GPIO_IN);
    gpio_set_dir(PIN_BTN_B, GPIO_IN);
    gpio_pull_up(PIN_BTN_A);
    gpio_pull_up(PIN_BTN_B);

    setup_display();

    uint adc_raw;
    uint offset;

    // This will find a free pio and state machine for our program and load it for us
    // We use pio_claim_free_sm_and_add_program_for_gpio_range (for_gpio_range variant)
    // so we will get a PIO instance suitable for addressing gpios >= 32 if needed and supported by the hardware
    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &sm, &offset, WS2812_PIN, 1, true);
    hard_assert(success);

    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);

    struct repeating_timer timer1, timer2;
    add_repeating_timer_ms(100, checkbutton_timer, NULL, &timer1);
    add_repeating_timer_ms(250, blink_cursor_timer, NULL, &timer2);

    reset();

    multicore_launch_core1(tarefa_no_nucleo1);

    while (1)
    {
        if (gameStatus == STATUS_RESTART)
        {
            reset();
        }
        on_ler_e_desenha();
        // tocar_som_final();
        sleep_ms(100);
    }

    pio_remove_program_and_unclaim_sm(&ws2812_program, pio, sm, offset);
}