// TCP Library

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include "tcp.h"
#include "eth0.h"
#include "timer.h"
#include "gpio.h"
#include "uart0.h"
#include "wait.h"

/* ========================
          TCP STATES
   ======================== */
   
// TCP Message Types
#define FIN 1
#define SYN 2
#define RST 4
#define PSH 8
#define ACK 16


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


/* ========================
          TCP GLOBALS
   ======================== */
uint8_t tcpState = 0;

bool synFlag = false;

/*  ========================== *
 *      TCP STATE FUNCTIONS    *
 *  ========================== */
uint8_t tcpGetState()
{
	return tcpState;
}

void tcpSetState(uint8_t state)
{
	if(tcpState == state)
		return;
	
	tcpState = state;
	
	putsUart0("TCP State set to: ");
	putcUart0(state + 48);
	putcUart0('\n');
	putcUart0('\n');
}


/*  ========================== *
 *         TCP  UTILITIES      *
 *  ========================== */
void tcpSendMessage(etherHeader *ether, SOCKET * s, uint8_t type)
{
	uint32_t sum = 0;
    uint8_t i, opt = 0, data_i = 0, ipHeaderLength;
    uint16_t tmp16;
    uint8_t mac[6], myIP[4];
	
	// Ether Header
	etherGetMacAddress(mac);
	for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = 0xFF;
        ether->sourceAddress[i] = mac[i];
    }
	ether->frameType = htons(0x800);
	
	// IP Header
	ipHeader* ip = (ipHeader*)ether->data;
    ip->revSize = 0x45;
    ipHeaderLength = (ip->revSize & 0xF) * 4;
    ip->typeOfService = 0;
    ip->id = 0;
    ip->flagsAndOffset = 0;
    ip->ttl = 128;
    ip->protocol = 6; // TCP
    ip->headerChecksum = 0;
	
	
	etherGetIpAddress(myIP);
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        ip->destIp[i] = s->svrIp[i]; // Send to SOCKET's server IP
        ip->sourceIp[i] = myIP[i]; // From our device IP, should also be SOCKET's device IP
    }
	
	
/* 
	uint16_t sourcePort;
	uint16_t destPort;
	uint32_t sequenceNumber;
	uint32_t acknowledgementNumber;
	uint16_t offsetFields;
	uint16_t windowSize;
	uint16_t checksum;
	uint16_t urgentPointer;
	uint8_t  data[0];
*/
	
	// TCP Header
	tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
	
	tcp->sourcePort = s->devPort;
	tcp->destPort = s->svrPort;
	
	if( tcpGetState() == TCP_CLOSED )
	{
		tcp->sequenceNumber = 0;
		tcp->acknowledgementNumber = 0;
	}

	tcp->urgentPointer = 0;

	// TCP Options
	// Have to send TCP options in 4 byte groups?
	/*tcp->data[opt++] = 1;
	tcp->data[opt++] = 1;
	tcp->data[opt++] = 1;
	tcp->data[opt++] = 1;*/

	// TCP Data
	data_i = opt; // for array indexing
	tcp->data[data_i++] = 'T';
	tcp->data[data_i++] = 'C';
	tcp->data[data_i++] = 'P';
	tcp->data[data_i++] = ' ';
	tcp->data[data_i++] = 'S';
	tcp->data[data_i++] = 'U';
	tcp->data[data_i++] = 'X';
	data_i -= opt; // get normal data size after done

	// Header Size Calc
	uint16_t tcpHeaderSize = sizeof(tcpHeader);
	tcpHeaderSize += opt;

	//
	uint16_t offset = 0;
	offset |= ( (tcpHeaderSize) / 4) << 12; // Size / 4 bytes << Shifted to upper 4 bits
	offset |= type;
	tcp->offsetFields = htons(offset);


	uint16_t tcpDataSize = data_i;
	uint16_t tcpTotalSize = tcpDataSize + tcpHeaderSize;

	ip->length = htons(tcpTotalSize + ipHeaderLength);
	etherCalcIpChecksum(ip);

	// psuedo-header
    etherSumWords(ip->sourceIp, 8, &sum);  // adds source and dest
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8; // should have '17' in upper 8 bits, 00 in lower 8 bits
    sum += htons( tcpTotalSize ); // TCP Length

    tcp->checksum = 0;
    etherSumWords(tcp, tcpTotalSize, &sum);

    tcp->checksum = getEtherChecksum(sum);

    etherPutPacket(ether, sizeof(etherHeader) + ipHeaderLength + tcpTotalSize);

}

void tcpSendPendingMessages(etherHeader *ether, SOCKET *s)
{
	if(synFlag)
	{
	    tcpSendMessage(ether, s, SYN);
	    synFlag = false;
	}
}

void tcpProcessTcpResponse(etherHeader *ether)
{
	
}

void tcpSynReq()
{
    synFlag = true;
}
