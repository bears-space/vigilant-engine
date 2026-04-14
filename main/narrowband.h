#pragma once
#ifdef __cplusplus
extern "C" {
#endif
 
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

/*
* Initializes the narrowband communication module, and starts the rxtx task which continuously transmits sensor data and listens for commands.
* @param commandQueue pointer to FreeRTOS queue for data received by narrowband module
* @param sensorDataQueue pointer to FreeRTOS queue for data to be transmitted by narrowband module
*/
void init_narrowband(QueueHandle_t* commandQueue, QueueHandle_t* sensorDataQueue);

#ifdef __cplusplus
}
#endif