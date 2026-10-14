#include <stdio.h>
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

gpio_num_t filas[4] = {GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_27, GPIO_NUM_26};
gpio_num_t columnas[4] = {GPIO_NUM_25, GPIO_NUM_33, GPIO_NUM_32, GPIO_NUM_23};

char teclado[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

extern "C" void app_main(void)
{
    // Configuración de filas como salidas (drenador abierto: dos teclas
    // de la misma columna no ponen en corto una fila en 1 con otra en 0)
    for (int i = 0; i < 4; i++) {
        gpio_reset_pin(filas[i]);
        gpio_set_direction(filas[i], GPIO_MODE_OUTPUT_OD);
        gpio_set_level(filas[i], 1);
    }

    // Configuración de columnas como entradas
    for (int i = 0; i < 4; i++) {
        gpio_reset_pin(columnas[i]);
        gpio_set_direction(columnas[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(columnas[i], GPIO_PULLUP_ONLY);
    }

    printf("Teclado matricial listo\n");

    while (1) {

        for (int f = 0; f < 4; f++) {

            gpio_set_level(filas[f], 0);
            esp_rom_delay_us(50); // tiempo de estabilización de la línea

            for (int c = 0; c < 4; c++) {

                if (gpio_get_level(columnas[c]) == 0) {

                    printf("Tecla: %c\n", teclado[f][c]);

                    vTaskDelay(pdMS_TO_TICKS(30));

                    while (gpio_get_level(columnas[c]) == 0) {
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }

                    // antirrebote al soltar
                    vTaskDelay(pdMS_TO_TICKS(30));
                }
            }

            gpio_set_level(filas[f], 1);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
