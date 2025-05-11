#include "include/tcp_socket_client.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_log.h"

#include "../../../include/comms.h"

#define STREAM_BUFFER_SIZE              500
#define STREAM_BUFFER_LENGTH_TRIGGER    15

#define PORT 8080


#define HOST_IP_ADDR         "192.168.0.100"

QueueHandle_t connectionStateQueueHandler;   // TODO: mejorar este mecanismo

// TODO: deberia venir por parametro
extern StreamBufferHandle_t xStreamBufferReceiver;
extern StreamBufferHandle_t xStreamBufferSender;

static const char *TAG = "TCP CLIENT";

uint8_t serverClientConnected = false;

static void newConnectionState(bool state) {
    serverClientConnected = state;
    if (xQueueOverwrite(connectionStateQueueHandler, &state) != pdPASS) {
        ESP_LOGE(TAG, "Error al enviar el nuevo estado de connection");
    }
}

static void tcpSocketReceiverTask(void *pvParameters) {
    int socket = *(int *) pvParameters;
    char rx_buffer[128];
    xStreamBufferReset(xStreamBufferReceiver);

    while (serverClientConnected) {

        int len = recv(socket, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len > 0) {
            xStreamBufferSend(xStreamBufferReceiver,rx_buffer,len,1);
        }

        vTaskDelay(pdMS_TO_TICKS(25));
    }
    vTaskDelete(NULL);
}

static void tcpSocketSender(int sock) {

    char received_data[100];

    ESP_LOGI(TAG, "socket content: %d", sock);

    xStreamBufferReset(xStreamBufferSender);
    
    while (serverClientConnected) {
        BaseType_t bytesStreamReceived = xStreamBufferReceive(xStreamBufferSender, received_data, sizeof(received_data), 0);

        if (bytesStreamReceived > 1) {
            int errSend = lwip_send(sock, received_data, bytesStreamReceived, 0);
            if (errSend < 0) {
                ESP_LOGE(TAG, "Error conexion perdida 1, errno %d", errno);
                break;
            } 
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

static void tcpClientSocket(void *pvParameters) {
    QueueHandle_t connectionStateQueueHandler = (QueueHandle_t)pvParameters;

    struct sockaddr_in dest_addr = {
        .sin_addr.s_addr = inet_addr(HOST_IP_ADDR),
        .sin_family = AF_INET,
        .sin_port = htons(PORT) 
    };

    while (true) {                        
        int sock =  socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // ipv4 , x, IPPROTO_IP;
        
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            shutdown(sock, 0);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            ESP_LOGI(TAG, "Socket created, connecting to %s:%d", HOST_IP_ADDR, PORT);

            if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6))) {
                close(sock);
                vTaskDelay(pdMS_TO_TICKS(500));
            } else {
                ESP_LOGI(TAG, "Successfully connected");
                comms_start_up();
                xTaskCreatePinnedToCore(tcpSocketReceiverTask, "tcp_client receiver", 4096, &sock, configMAX_PRIORITIES - 2, NULL, COMMS_HANDLER_CORE);

                newConnectionState(true);
                tcpSocketSender(sock);
                newConnectionState(false);
                
                if (sock != -1) {
                    ESP_LOGE(TAG, "Shutting down socket and restarting...");
                    shutdown(sock, 0);
                    close(sock);
                }
            }
        }
    }
    vTaskDelete(NULL);
}

void initTcpClientSocket(QueueHandle_t connectionQueueHandler) {
    connectionStateQueueHandler = connectionQueueHandler;
    xStreamBufferSender = xStreamBufferCreate(STREAM_BUFFER_SIZE, STREAM_BUFFER_LENGTH_TRIGGER);
    xStreamBufferReceiver = xStreamBufferCreate(STREAM_BUFFER_SIZE, STREAM_BUFFER_LENGTH_TRIGGER);
    xTaskCreatePinnedToCore(tcpClientSocket, "tcp client task", 4096, connectionStateQueueHandler,configMAX_PRIORITIES - 1, NULL, COMMS_HANDLER_CORE);
}
