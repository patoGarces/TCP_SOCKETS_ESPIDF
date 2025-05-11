# TCP Socket Component for ESP-IDF (Servidor y Cliente)

Este componente proporciona una abstracción sencilla para manejar comunicación TCP en ESP-IDF, tanto en modo **servidor** como **cliente**. La comunicación se realiza mediante **Stream Buffers** para el envío y la recepción de datos, y una **Queue** para indicar el estado de conexión.

This component provides a simple abstraction for handling TCP communication in ESP-IDF, both in **server** and **client** modes. It uses **Stream Buffers** for sending and receiving data, and a **Queue** to signal connection status.

## 🔧 Uso Básico / Basic Usage

### Definición externa / External definition

```c
QueueHandle_t socketConnectionStateQueueHandler;
StreamBufferHandle_t xStreamBufferReceiver;
StreamBufferHandle_t xStreamBufferSender;

socketConnectionStateQueueHandler = xQueueCreate(1, sizeof(uint8_t));
initTcpServerSocket(socketConnectionStateQueueHandler);
```

### Recepción / Receiving data

```c
uint8_t received_data[128];
BaseType_t bytes_received = xStreamBufferReceive(
    xStreamBufferReceiver,
    received_data,
    sizeof(received_data),
    0);
```

### Envío / Sending data

```c
if (xStreamBufferSend(xStreamBufferSender, &dynamicData, sizeof(dynamicData), 1) != sizeof(dynamicData)) {
    ESP_LOGI("COMMS", "Overflow stream buffer dynamic data, is full?: %d, resetting...",
             xStreamBufferIsFull(xStreamBufferSender));
    xStreamBufferReset(xStreamBufferSender);
}
```

## 🔁 Notificación de conexión / Connection notification

* La queue `socketConnectionStateQueueHandler` notifica la cantidad de clientes conectados al servidor.
* El usuario puede hacer `xQueueReceive()` para saber cuándo se conecta o desconecta un cliente.

`socketConnectionStateQueueHandler` notifies how many clients are connected to the server. The user can call `xQueueReceive()` to get updates on connection/disconnection events.

## 👥 Límite de clientes / Client limit

* El número máximo de clientes se configura con la macro:

```c
#define MAX_TCP_CLIENTS 2
```

This constant sets the maximum number of simultaneous TCP clients the server will accept.

## 🔁 Reconexión automática / Auto-reconnect

* En modo **servidor**, si un cliente se desconecta, el componente acepta nuevos clientes automáticamente.
* En modo **cliente**, si se pierde la conexión, el componente reintenta conectarse periódicamente.

In server mode, the component automatically accepts new clients when a previous one disconnects. In client mode, it will retry connection periodically if disconnected.

## 📚 Estructura / Structure

```
tcp_socket_component/
├── include/
│   └── tcp_socket_component.h   // API pública / Public API
├── tcp_socket_server.c          // Implementación modo servidor / Server logic
└── tcp_socket_client.c          // Implementación modo cliente / Client logic
```

## 📤 API principal / Main API

```c
extern StreamBufferHandle_t xStreamBufferReceiver;
extern StreamBufferHandle_t xStreamBufferSender;

void initTcpServerSocket(QueueHandle_t connectionQueue);
void initTcpClientSocket(QueueHandle_t connectionQueue);
```

Estas funciones deben llamarse desde el `main`, pasando la `Queue` creada por el usuario. Los buffers son declarados como `extern`, por lo que deben enlazarse correctamente en el `main.c`.

These functions are meant to be called from `main.c`, passing a user-created `Queue`. The buffers are `extern`, so they must be defined in your `main.c` file.

---
