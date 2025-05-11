#ifndef TCP_SOCKET_SERVER_H
#define TCP_SOCKET_SERVER_H

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define TCP_CLIENT_CORE 0

#define MAX_CLIENTS_CONNECTED   3

void initTcpServerSocket(QueueHandle_t connectionQueueHandler);

#endif