#include "include/tcp_socket_component.h"

#include <string.h>
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_log.h"

#include "../../../include/comms.h"

#define PORT 8080

#define HOST_IP_ADDR         "192.168.0.100"

static tcp_socket_config_t socketConfig;

static const char *TAG = "TCP CLIENT";

uint8_t serverClientConnected = false;

static void newConnectionState(bool state) {
    serverClientConnected = state;
    if (xQueueOverwrite(socketConfig.connectionQueueHandler, &state) != pdPASS) {
        ESP_LOGE(TAG, "Error al enviar el nuevo estado de connection");
    }
}

static void tcpSocketReceiverTask(void *pvParameters) {
    int socket = *(int *) pvParameters;
    char rx_buffer[128];
    xStreamBufferReset(socketConfig.xStreamBufferRecv);

    while (serverClientConnected) {

        int len = recv(socket, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len > 0) {
            xStreamBufferSend(socketConfig.xStreamBufferRecv,rx_buffer,len,1);
        }

        vTaskDelay(pdMS_TO_TICKS(25));
    }
    vTaskDelete(NULL);
}

static void tcpSocketSender(int sock) {

    char received_data[100];

    ESP_LOGI(TAG, "socket content: %d", sock);

    xStreamBufferReset(socketConfig.xStreamBufferSend);
    
    while (serverClientConnected) {
        BaseType_t bytesStreamReceived = xStreamBufferReceive(socketConfig.xStreamBufferSend, received_data, sizeof(received_data), 0);

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
                xTaskCreatePinnedToCore(tcpSocketReceiverTask, "tcp_client receiver", 4096, &sock, configMAX_PRIORITIES - 2, NULL, TCP_SOCKET_CORE);

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

void initTcpClientSocket(tcp_socket_config_t config) {
    socketConfig = config;
    xTaskCreatePinnedToCore(tcpClientSocket, "tcp client task", 4096, NULL, configMAX_PRIORITIES - 1, NULL, TCP_SOCKET_CORE);
}
