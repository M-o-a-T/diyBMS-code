#ifndef influxdb_H_
#define influxdb_H_

#ifdef USE_INFLUXDB

#include "defines.h"

#include <WiFi.h>
#include <AsyncTCP.h>


void influxdb_onConnect(void *arg, AsyncClient *client);
void influxdb_onError(void *arg, AsyncClient *client, err_t error);
void influxdb_onData(void *arg, AsyncClient *client, void *data, size_t len);


char hex_digit(char c);
String urlEncode(const char *src);
void influx_task_action();

extern uint16_t TotalNumberOfCells();

#endif // USE
#endif // guard
