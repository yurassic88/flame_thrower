#ifndef HTTP_LOG_H
#define HTTP_LOG_H

#include "lwip/init.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_tls.h"

#define xBufferSizeBytes 2251
// The size, in bytes, required to hold each item in the message,
#define xItemSize 150


typedef struct {
	uint16_t port;
	char ipv4[20]; // xxx.xxx.xxx.xxx
	char url[64]; // mqtt://iot.eclipse.org
	char topic[64];
	TaskHandle_t taskHandle;
} PARAMETER_t;

int logging_vprintf( const char *fmt, va_list l );
esp_err_t http_logging_init(char *url, int16_t enableStdout);

#endif