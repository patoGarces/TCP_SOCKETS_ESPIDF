#include "include/tcp_socket_component.h"

#include <string.h>
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_log.h"

#include "../../../include/comms.h"

#define STREAM_BUFFER_SIZE              500
#define STREAM_BUFFER_LENGTH_TRIGGER    15

#define PORT 8080

QueueHandle_t connectionStateQueueHandler;

static const char *TAG = "TCP SERVER";

uint8_t serverClientConnected = false;
int8_t listOfClients[MAX_CLIENTS_CONNECTED];
uint8_t socketCounter = 0;

static void tcpSocketSenderTask(void *pvParameters);

static void updateConnectionState() {
    serverClientConnected = socketCounter > 0;
    if (xQueueOverwrite(connectionStateQueueHandler, &socketCounter) != pdPASS) {
        ESP_LOGE(TAG, "Error al enviar el nuevo estado de connection");
    }
}

static void newClientConnected(int8_t sock) {
    listOfClients[socketCounter] = sock;
    socketCounter++;

    if (socketCounter == 1) {
        comms_start_up();
        xTaskCreatePinnedToCore(tcpSocketSenderTask, "tcp server sender", 4096, NULL, configMAX_PRIORITIES - 2, NULL, TCP_SOCKET_CORE);
    }
    updateConnectionState();
}

static void removeClientConnected(uint8_t sock) {
    for(uint8_t i=0; i< socketCounter; i++) {
        if(listOfClients[i] == sock) {
            shutdown(sock, 0);
            close(sock);
        }

        // Muevo el último socket a la posición del eliminado, para no tener "huecos" en la lista
        listOfClients[i] = listOfClients[socketCounter - 1];
        socketCounter--;
        updateConnectionState();
        return;
    }

    ESP_LOGE(TAG, "Socket %d no encontrado para eliminar", sock);
}

static void tcpSocketReceiver(int8_t sock) {
    char rx_buffer[128];
    xStreamBufferReset(xStreamBufferReceiver);

    while (serverClientConnected) {

        int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len > 0) {
            xStreamBufferSend(xStreamBufferReceiver,rx_buffer,len,1);
        } else {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

/* Hago un broadcast de los datos enviados a todos los clientes, todos reciben la misma data*/
static void tcpSocketSenderTask(void *pvParameters) {

    char received_data[100];
    xStreamBufferReset(xStreamBufferSender);
    
    while (serverClientConnected) {
        BaseType_t bytesStreamReceived = xStreamBufferReceive(xStreamBufferSender, received_data, sizeof(received_data), 0);

        if (bytesStreamReceived > 1) {

            for(uint8_t i=0; i< socketCounter; i++) {
                if (listOfClients[i] > 0) {
                    int errSend = lwip_send(listOfClients[i], received_data, bytesStreamReceived, 0);
                    if (errSend < 0) {
                        ESP_LOGE(TAG, "Error conexion perdida 1, errno %d", errno);
                        removeClientConnected(listOfClients[i]);
                        i--;            // elimine el actual, voy al siguiente
                    } 
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }

    vTaskDelete(NULL);
}

static void socketClientsHandlerTask(void *pvParameters) {

    int sock = *(int *) pvParameters;

    // Calculo demora en al deteccion de perdida de conexion: keepIdle + (keepInterval × keepCount) 
    int keepAlive = 1;
    int keepIdle = 1;           // segundos sin actividad
    int keepInterval = 1;       // periodo de reintento
    int keepCount = 2;          // cantidad de intentos

    // Set tcp keepalive option
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

    newClientConnected(sock);
    ESP_LOGI(TAG, "new socket client connected: %d", socketCounter);
    
    tcpSocketReceiver(sock);    // loop mientras ese socket este abierto

    removeClientConnected(sock);

    vTaskDelete(NULL);
}

static void socketOrchestrator(void *pvParameters) { 
    int addr_family = AF_INET;

    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        close(listen_sock);
        vTaskDelete(NULL);
    }

    err = listen(listen_sock, 2);               // Hasta 2 conexiones pendientes
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "Socket listening");

    while (1) {
        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);

        int8_t sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        if (socketCounter < MAX_CLIENTS_CONNECTED) {
            xTaskCreatePinnedToCore(socketClientsHandlerTask, "tcp server socket clients handler task", 4096, &sock, configMAX_PRIORITIES - 1, NULL, TCP_SOCKET_CORE);
        } else {
            close(sock);
            ESP_LOGE(TAG, "Max client limit reached, socket client closed");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    close(listen_sock);
    vTaskDelete(NULL);
}

void initTcpServerSocket(QueueHandle_t connectionQueueHandler) {
    connectionStateQueueHandler = connectionQueueHandler;
    xStreamBufferSender = xStreamBufferCreate(STREAM_BUFFER_SIZE, STREAM_BUFFER_LENGTH_TRIGGER);
    xStreamBufferReceiver = xStreamBufferCreate(STREAM_BUFFER_SIZE, STREAM_BUFFER_LENGTH_TRIGGER);
    xTaskCreatePinnedToCore(socketOrchestrator, "socket orchestrator task", 4096, NULL,configMAX_PRIORITIES - 1, NULL, TCP_SOCKET_CORE);
}