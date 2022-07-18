
#ifndef MAX_PACKET_SIZE
#define MAX_PACKET_SIZE 16
#endif

#ifdef DEBUG
#define DBG(x) Serial.print(F(x))
#define DBGV(x) Serial.print(x)
#define DBGN(x) Serial.println(F(x))
#else
#define DBG(x) do {} while(0)
#define DBGV(x) do {} while(0)
#define DBGN(x) do {} while(0)
#endif

#include "diybms_common.h"

