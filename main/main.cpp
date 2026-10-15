#include <stdio.h>
#include <inttypes.h>
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Cambia a true si tus displays son de ánodo común
#define ANODO_COMUN false

// Nivel del GPIO que enciende un display:
//   cátodo común + transistor NPN -> 1
//   ánodo común  + transistor PNP -> 0
#define DIGITO_ON 1

// Estados de la calculadora
#define PIDE_NUM1   0
#define PIDE_OP     1
#define PIDE_NUM2   2
#define PIDE_IGUAL  3
#define RESULTADO   4

// Patrones especiales (bit 0 = a ... bit 6 = g)
#define APAGADO 0x00
#define MENOS   0x40   // solo segmento g
#define LETRA_E 0x79   // segmentos a, d, e, f, g

// Límites de la calculadora
#define MAX_CIFRAS_9 100000000   // un número >= a este ya tiene 9 cifras
#define RESULTADO_MAX 99         // lo más grande que cabe en dos displays
#define RESULTADO_MIN -9         // "-" en un display y el dígito en el otro

gpio_num_t filas[4] = {GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_27, GPIO_NUM_26};
gpio_num_t columnas[4] = {GPIO_NUM_25, GPIO_NUM_33, GPIO_NUM_32, GPIO_NUM_23};

// Segmentos en orden: a, b, c, d, e, f, g (compartidos por los dos displays)
gpio_num_t segmentos[7] = {GPIO_NUM_4, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18,
                           GPIO_NUM_19, GPIO_NUM_21, GPIO_NUM_22};

// Común de cada display (a través del transistor)
gpio_num_t comun_izq = GPIO_NUM_5;    // decenas
gpio_num_t comun_der = GPIO_NUM_15;   // unidades

char teclado[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

// Patrones de los dígitos 0 a 9
uint8_t digitos[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

// Lo que debe mostrar cada display (el timer los lee todo el tiempo)
volatile uint8_t patron_izq = APAGADO;
volatile uint8_t patron_der = APAGADO;

// Variables de la calculadora
int estado = PIDE_NUM1;
int num1 = 0;
int num2 = 0;
char operacion = ' ';

// ---------- Displays ----------

void mostrar(uint8_t patron)
{
    for (int s = 0; s < 7; s++) {
        int nivel = (patron >> s) & 1;
        if (ANODO_COMUN) {
            nivel = !nivel;
        }
        gpio_set_level(segmentos[s], nivel);
    }
}

// Se ejecuta cada 5 ms: apaga los dos displays, carga los segmentos
// y enciende solo uno. En la siguiente llamada le toca al otro.
void refrescar_displays(void *arg)
{
    static int turno = 0;

    gpio_set_level(comun_izq, !DIGITO_ON);
    gpio_set_level(comun_der, !DIGITO_ON);

    if (turno == 0) {
        mostrar(patron_izq);
        gpio_set_level(comun_izq, DIGITO_ON);
    } else {
        mostrar(patron_der);
        gpio_set_level(comun_der, DIGITO_ON);
    }

    turno = !turno;
}

void mostrar_numero(int n)
{
    if (n < 0) {                        // negativo: "-" y el valor
        patron_izq = MENOS;
        patron_der = digitos[-n];
    } else if (n < 10) {                // un dígito: izquierdo apagado
        patron_izq = APAGADO;
        patron_der = digitos[n];
    } else {                            // dos dígitos: decenas y unidades
        patron_izq = digitos[n / 10];
        patron_der = digitos[n % 10];
    }
}

// Mientras se teclea un número solo se ven sus dos últimas cifras
void mostrar_entrada(int n)
{
    if (n < 10) {
        patron_izq = APAGADO;
    } else {
        patron_izq = digitos[(n / 10) % 10];
    }
    patron_der = digitos[n % 10];
}

void mostrar_error(void)
{
    patron_izq = LETRA_E;
    patron_der = APAGADO;
}

void apagar_displays(void)
{
    patron_izq = APAGADO;
    patron_der = APAGADO;
}

// ---------- Calculadora ----------

// Símbolo y nombre de cada tecla de operación, para la terminal
char simbolo(char op)
{
    if (op == 'A') return '+';
    if (op == 'B') return '-';
    if (op == 'C') return 'x';
    if (op == 'D') return '/';
    return '?';
}

const char *nombre(char op)
{
    if (op == 'A') return "suma";
    if (op == 'B') return "resta";
    if (op == 'C') return "multiplicacion";
    if (op == 'D') return "division";
    return "desconocida";
}

// Agrega una cifra al número que se está tecleando (máximo 9 cifras).
// Los displays muestran las dos últimas y la terminal el número completo.
void agregar_cifra(int &num, char tecla, const char *etiqueta)
{
    if (num >= MAX_CIFRAS_9) {
        printf("Maximo 9 cifras, tecla ignorada\n");
        return;
    }
    num = num * 10 + (tecla - '0');
    mostrar_entrada(num);
    printf("%s: %d\n", etiqueta, num);
}

void calcular(void)
{
    // En 64 bits: 999999999 x 999999999 no se desborda
    int64_t resultado = 0;

    if (operacion == 'A') {
        resultado = (int64_t)num1 + num2;
    } else if (operacion == 'B') {
        resultado = (int64_t)num1 - num2;
    } else if (operacion == 'C') {
        resultado = (int64_t)num1 * num2;
    } else if (operacion == 'D') {
        if (num2 == 0) {
            mostrar_error();
            printf("%d / 0 = Error\n", num1);
            return;
        }
        resultado = num1 / num2;        // división entera: trunca
    }

    if (resultado > RESULTADO_MAX || resultado < RESULTADO_MIN) {
        mostrar_error();
        printf("%d %c %d = %" PRId64 " -> no cabe en el display\n",
               num1, simbolo(operacion), num2, resultado);
        return;
    }

    mostrar_numero((int)resultado);
    printf("%d %c %d = %" PRId64 "\n", num1, simbolo(operacion), num2, resultado);
}

void procesar_tecla(char tecla)
{
    // '*' borra todo desde cualquier estado
    if (tecla == '*') {
        estado = PIDE_NUM1;
        apagar_displays();
        printf("Borrado\n");
        return;
    }

    int es_digito = (tecla >= '0' && tecla <= '9');
    int es_operacion = (tecla == 'A' || tecla == 'B' || tecla == 'C' || tecla == 'D');

    if (estado == PIDE_NUM1) {
        if (es_digito) {
            num1 = 0;
            agregar_cifra(num1, tecla, "Num1");
            estado = PIDE_OP;
        }
    }
    else if (estado == PIDE_OP) {
        if (es_digito) {                // sigue tecleando el primer número
            agregar_cifra(num1, tecla, "Num1");
        } else if (es_operacion) {
            operacion = tecla;
            printf("Operacion: %s (%c)\n", nombre(operacion), simbolo(operacion));
            estado = PIDE_NUM2;
        }
    }
    else if (estado == PIDE_NUM2) {
        if (es_digito) {
            num2 = 0;
            agregar_cifra(num2, tecla, "Num2");
            estado = PIDE_IGUAL;
        }
    }
    else if (estado == PIDE_IGUAL) {
        if (es_digito) {                // sigue tecleando el segundo número
            agregar_cifra(num2, tecla, "Num2");
        } else if (tecla == '#') {
            calcular();
            estado = RESULTADO;
        }
    }
    else if (estado == RESULTADO) {
        if (es_digito) {                // empieza una operación nueva
            num1 = 0;
            agregar_cifra(num1, tecla, "Num1");
            estado = PIDE_OP;
        }
    }
}

// ---------- Programa principal ----------

extern "C" void app_main(void)
{
    // Filas como salidas (drenador abierto)
    for (int i = 0; i < 4; i++) {
        gpio_reset_pin(filas[i]);
        gpio_set_direction(filas[i], GPIO_MODE_OUTPUT_OD);
        gpio_set_level(filas[i], 1);
    }

    // Columnas como entradas con pull-up
    for (int i = 0; i < 4; i++) {
        gpio_reset_pin(columnas[i]);
        gpio_set_direction(columnas[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(columnas[i], GPIO_PULLUP_ONLY);
    }

    // Segmentos como salidas
    for (int i = 0; i < 7; i++) {
        gpio_reset_pin(segmentos[i]);
        gpio_set_direction(segmentos[i], GPIO_MODE_OUTPUT);
    }

    // Comunes como salidas, empiezan apagados
    gpio_reset_pin(comun_izq);
    gpio_reset_pin(comun_der);
    gpio_set_direction(comun_izq, GPIO_MODE_OUTPUT);
    gpio_set_direction(comun_der, GPIO_MODE_OUTPUT);
    gpio_set_level(comun_izq, !DIGITO_ON);
    gpio_set_level(comun_der, !DIGITO_ON);

    // Timer que refresca los displays cada 5 ms
    esp_timer_create_args_t config = {};
    config.callback = &refrescar_displays;
    config.name = "displays";

    esp_timer_handle_t timer;
    esp_timer_create(&config, &timer);
    esp_timer_start_periodic(timer, 5000);   // en microsegundos

    printf("Calculadora lista\n");

    while (1) {

        for (int f = 0; f < 4; f++) {

            gpio_set_level(filas[f], 0);
            esp_rom_delay_us(50);

            for (int c = 0; c < 4; c++) {

                if (gpio_get_level(columnas[c]) == 0) {

                    printf("Tecla: %c\n", teclado[f][c]);
                    procesar_tecla(teclado[f][c]);

                    vTaskDelay(pdMS_TO_TICKS(30));

                    while (gpio_get_level(columnas[c]) == 0) {
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }

                    vTaskDelay(pdMS_TO_TICKS(30));
                }
            }

            gpio_set_level(filas[f], 1);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}