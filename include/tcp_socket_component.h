#ifndef TCP_SOCKET_COMPONENT_H
#define TCP_SOCKET_COMPONENT_H

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"

#define TCP_SOCKET_CORE 0

#define MAX_CLIENTS_CONNECTED   3

extern StreamBufferHandle_t xStreamBufferReceiver;
extern StreamBufferHandle_t xStreamBufferSender;

void initTcpServerSocket(QueueHandle_t connectionQueueHandler);
void initTcpClientSocket(QueueHandle_t connectionQueueHandler);

#endif