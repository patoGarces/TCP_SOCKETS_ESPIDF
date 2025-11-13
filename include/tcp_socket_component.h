#ifndef TCP_SOCKET_COMPONENT_H
#define TCP_SOCKET_COMPONENT_H

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"

#define TCP_SOCKET_CORE 0

#define MAX_CLIENTS_CONNECTED   3

typedef struct {
    QueueHandle_t connectionQueueHandler;
    StreamBufferHandle_t xStreamBufferSend;
    StreamBufferHandle_t xStreamBufferRecv;
}tcp_socket_config_t;

void initTcpServerSocket(tcp_socket_config_t config);
void initTcpClientSocket(tcp_socket_config_t config);

#endif