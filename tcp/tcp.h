#ifndef _TCP_H_
#define _TCP_H_


#include <stdint.h>
#include <stdbool.h>
#include "eth0.h"

#define TCP_CLOSED          0 // "fictional state"
#define TCP_LISTEN          1
#define TCP_SYN_SENT        2
#define TCP_SYN_RECIEVED    3
#define TCP_ESTABLISHED     4
#define TCP_FIN_WAIT1       5
#define TCP_FIN_WAIT2       6
#define TCP_CLOSE_WAIT      7
#define TCP_CLOSING         8
#define TCP_LAST_ACK        9
#define TCP_TIME_WAIT       10

typedef struct _SOCKET
{
	uint8_t devIp[4];
	uint8_t svrIp[4];
	uint8_t svrAddress[6];
	uint16_t devPort;
	uint16_t svrPort;
	uint32_t sequenceNumber;
	uint32_t acknowledgementNumber;
} SOCKET;

void tcpSendMessage(etherHeader *ether, SOCKET * s, uint8_t type);

void tcpSendPendingMessages(etherHeader *ether, SOCKET *s);

void tcpProcessTcpResponse(etherHeader *ether, SOCKET *s);

void tcpSynReq(void);
void tcpFinReq(void);

#endif
