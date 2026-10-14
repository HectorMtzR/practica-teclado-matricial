#include <stdio.h>
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Cambia a true si tu display es de ánodo común
#define ANODO_COMUN false

gpio_num_t filas[4] = {GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_27, GPIO_NUM_26};
gpio_num_t columnas[4] = {GPIO_NUM_25, GPIO_NUM_33, GPIO_NUM_32, GPIO_NUM_23};

// Segmentos en orden: a, b, c, d, e, f, g
gpio_num_t segmentos[7] = {GPIO_NUM_4, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18,
                           GPIO_NUM_19, GPIO_NUM_21, GPIO_NUM_22};

char teclado[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

// Patrón de cada tecla, en la misma posición que en "teclado".
// Bit 0 = a, bit 1 = b, ... bit 6 = g  (1 = segmento encendido)
uint8_t patrones[4][4] = {
    {0x06, 0x5B, 0x4F, 0x77},   // 1  2  3  A
    {0x66, 0x6D, 0x7D, 0x7C},   // 4  5  6  b
    {0x07, 0x7F, 0x6F, 0x39},   // 7  8  9  C
    {0x7F, 0x3F, 0x7F, 0x5E}    // *  0  #  d   (* y # = todos encendidos)
};

void mostrar(uint8_t patron)
{
    for (int s = 0; s < 7; s++) {
        int nivel = (patron >> s) & 1;   // saca el bit del segmento s
        if (ANODO_COMUN) {
            nivel = !nivel;              // en ánodo común, 0 enciende
        }
        gpio_set_level(segmentos[s], nivel);
    }
}

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

    // Segmentos como salidas normales
    for (int i = 0; i < 7; i++) {
        gpio_reset_pin(segmentos[i]);
        gpio_set_direction(segmentos[i], GPIO_MODE_OUTPUT);
    }

    mostrar(0x00);   // display apagado al iniciar
    printf("Teclado y display listos\n");

    while (1) {

        for (int f = 0; f < 4; f++) {

            gpio_set_level(filas[f], 0);
            esp_rom_delay_us(50);

            for (int c = 0; c < 4; c++) {

                if (gpio_get_level(columnas[c]) == 0) {

                    printf("Tecla: %c\n", teclado[f][c]);
                    mostrar(patrones[f][c]);

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