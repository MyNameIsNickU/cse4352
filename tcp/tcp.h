#ifndef _TCP_H_
#define _TCP_H_


#include <stdint.h>
#include <stdbool.h>
#include "eth0.h"

typedef struct _SOCKET
{
	uint8_t devIp[4];
	uint8_t svrIp[4];
	uint16_t devPort;
	uint16_t svrPort;
	uint32_t sequenceNumber;
} SOCKET;

void tcpSendMessage(etherHeader *ether, SOCKET * s, uint8_t type);

void tcpSendPendingMessages(etherHeader *ether, SOCKET *s);

void tcpProcessTcpResponse(etherHeader *ether, SOCKET *s);

void tcpSynReq(void);

#endif
