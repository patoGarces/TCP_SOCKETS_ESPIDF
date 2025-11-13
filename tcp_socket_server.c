#include "include/tcp_socket_component.h"

#include <string.h>
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_log.h"

#include "../../../include/comms.h"

#define PORT 8080

static const char *TAG = "TCP SERVER";

static tcp_socket_config_t socketConfig;
static uint8_t serverClientConnected = false;
static int8_t listOfClients[MAX_CLIENTS_CONNECTED];
static uint8_t socketCounter = 0;

static TaskHandle_t senderTaskHandle = NULL;

static void tcpSocketSenderTask(void *pvParameters);

static void updateConnectionState() {
    serverClientConnected = socketCounter > 0;
    if (xQueueOverwrite(socketConfig.connectionQueueHandler, &socketCounter) != pdPASS) {
        ESP_LOGE(TAG, "Error al enviar el nuevo estado de connection");
    }
}

static void newClientConnected(int8_t sock) {
    listOfClients[socketCounter] = sock;
    socketCounter++;

    if (socketCounter == 1) {
        comms_start_up();
        xStreamBufferReset(socketConfig.xStreamBufferSend);
        xTaskCreatePinnedToCore(tcpSocketSenderTask, "tcp server sender", 4096, NULL, configMAX_PRIORITIES - 2, &senderTaskHandle, TCP_SOCKET_CORE);
    }
    updateConnectionState();
    ESP_LOGI(TAG, "Cliente conectado. Total: %d", socketCounter);
}

static void removeClientConnected(int8_t sock) {
    bool found = false;
    
    for(uint8_t i = 0; i < socketCounter; i++) {
        if(listOfClients[i] == sock) {
            ESP_LOGI(TAG, "Cerrando socket %d en posición %d", sock, i);
            shutdown(sock, SHUT_RDWR);
            close(sock);
            
            // Muevo el último socket a la posición del eliminado
            listOfClients[i] = listOfClients[socketCounter - 1];
            listOfClients[socketCounter - 1] = -1; // Limpio la última posición
            socketCounter--;
            found = true;
            break; // CRÍTICO: salir después de encontrar
        }
    }

    if (!found) {
        ESP_LOGE(TAG, "Socket %d no encontrado para eliminar", sock);
        return;
    }

    updateConnectionState();
    ESP_LOGI(TAG, "Cliente desconectado. Quedan: %d", socketCounter);
    
    // Si no quedan clientes, el sender task se auto-eliminará
    if (socketCounter == 0) {
        ESP_LOGI(TAG, "No quedan clientes conectados");
    }
}

static void tcpSocketReceiver(int8_t sock) {
    char rx_buffer[128];
    int consecutiveErrors = 0;

    while (serverClientConnected) {
        int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        
        if (len > 0) {
            consecutiveErrors = 0;
            xStreamBufferSend(socketConfig.xStreamBufferRecv, rx_buffer, len, pdMS_TO_TICKS(10));
        } 
        else if (len == 0) {
            // Cliente cerró la conexión ordenadamente
            ESP_LOGI(TAG, "Cliente %d cerró la conexión", sock);
            break;
        } 
        else {
            // Error en recv
            consecutiveErrors++;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No hay datos disponibles, continuar
                consecutiveErrors = 0;
            } else {
                ESP_LOGE(TAG, "Error recv socket %d: errno %d", sock, errno);
                if (consecutiveErrors > 3) {
                    break;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(25));
    }
    
    ESP_LOGI(TAG, "Saliendo de receiver para socket %d", sock);
}

/* Broadcast de datos a todos los clientes */
static void tcpSocketSenderTask(void *pvParameters) {
    char received_data[100];
    
    ESP_LOGI(TAG, "Sender task iniciado");
    
    while (serverClientConnected) {
        BaseType_t bytesStreamReceived = xStreamBufferReceive(socketConfig.xStreamBufferSend, received_data, sizeof(received_data), pdMS_TO_TICKS(100));

        if (bytesStreamReceived > 0) {
            // Iterar sobre copia de socketCounter por si cambia durante el envío
            uint8_t currentSocketCount = socketCounter;
            
            for(uint8_t i = 0; i < currentSocketCount && i < socketCounter; i++) {
                if (listOfClients[i] >= 0) {
                    int errSend = lwip_send(listOfClients[i], received_data, bytesStreamReceived, 0);
                    if (errSend < 0) {
                        ESP_LOGE(TAG, "Error enviando a socket %d, errno %d", listOfClients[i], errno);
                        int8_t sockToRemove = listOfClients[i];
                        removeClientConnected(sockToRemove);
                        
                        // Reiniciar el loop ya que la lista cambió
                        break;
                    } 
                }
            }
        }
        
        // Pequeña pausa para no saturar
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Sender task finalizando");
    senderTaskHandle = NULL;
    vTaskDelete(NULL);
}

static void socketClientsHandlerTask(void *pvParameters) {
    // CRÍTICO: copiar el valor inmediatamente
    int sock = *((int *) pvParameters);
    
    // Liberar el semáforo para que socketOrchestrator pueda continuar
    vTaskDelay(pdMS_TO_TICKS(10));

    // Configuración keepalive más agresiva
    int keepAlive = 1;
    int keepIdle = 5;           // segundos sin actividad
    int keepInterval = 3;       // periodo de reintento
    int keepCount = 3;          // cantidad de intentos
    
    // Timeout de recepción
    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;

    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    newClientConnected(sock);
    
    tcpSocketReceiver(sock);    // loop mientras ese socket esté abierto

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
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    err = listen(listen_sock, MAX_CLIENTS_CONNECTED);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Socket listening on port %d", PORT);

    while (1) {
        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);

        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (socketCounter < MAX_CLIENTS_CONNECTED) {
            // CRÍTICO: pasar copia del socket, no la dirección de variable local
            static int sock_copy;
            sock_copy = sock;
            
            BaseType_t taskCreated = xTaskCreatePinnedToCore(
                socketClientsHandlerTask, 
                "tcp client handler", 
                4096, 
                &sock_copy,  // Pasar dirección de variable estática
                configMAX_PRIORITIES - 1, 
                NULL, 
                TCP_SOCKET_CORE
            );
            
            if (taskCreated != pdPASS) {
                ESP_LOGE(TAG, "Error creando task para cliente");
                close(sock);
            } else {
                // Dar tiempo a que la task copie el valor
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        } else {
            ESP_LOGW(TAG, "Max client limit reached (%d), rechazando conexión", MAX_CLIENTS_CONNECTED);
            close(sock);
        }
    }
    
    close(listen_sock);
    vTaskDelete(NULL);
}

void initTcpServerSocket(tcp_socket_config_t config) {
    socketConfig = config;
    
    // Inicializar lista de clientes
    for(uint8_t i = 0; i < MAX_CLIENTS_CONNECTED; i++) {
        listOfClients[i] = -1;
    }
    
    socketCounter = 0;
    serverClientConnected = false;
    senderTaskHandle = NULL;
    
    if (socketConfig.xStreamBufferSend == NULL || socketConfig.xStreamBufferRecv == NULL) {
        ESP_LOGE(TAG, "Error creando stream buffers");
        return;
    }
    
    xTaskCreatePinnedToCore(
        socketOrchestrator, 
        "socket orchestrator", 
        4096, 
        NULL,
        configMAX_PRIORITIES - 1, 
        NULL, 
        TCP_SOCKET_CORE
    );
    
    ESP_LOGI(TAG, "TCP Server inicializado");
}