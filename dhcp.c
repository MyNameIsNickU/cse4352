// DHCP Library
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: -
// Target uC:       -
// System Clock:    -

// Hardware configuration:
// -

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "dhcp.h"
#include "eth0.h"
#include "timer.h"
#include "gpio.h"
#include "uart0.h"
#include "wait.h"

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPDECLINE  4
#define DHCPACK      5
#define DHCPNAK      6
#define DHCPRELEASE  7
#define DHCPINFORM   8

#define DHCP_DISABLED   0
#define DHCP_INIT       1
#define DHCP_SELECTING  2
#define DHCP_REQUESTING 3
#define DHCP_TESTING_IP 4
#define DHCP_BOUND      5
#define DHCP_RENEWING   6
#define DHCP_REBINDING  7
#define DHCP_INITREBOOT 8 // not used since ip not stored over reboot
#define DHCP_REBOOTING  9 // not used since ip not stored over reboot

#define MAGIC_COOKIE 0x63825363

#define BLUE_LED PORTF,2

// How many failed DHCPDISCOVER msgs will send before system reboot
#define MAX_FAILED_DISCOVERS 2
#define MAX_FAILED_REQUESTS 4

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

uint8_t dhcpState = DHCP_DISABLED;
uint32_t local_xid;

// bool flags can go here
bool renewFlag = false;
bool rebindFlag = false;
bool releaseFlag = false;
bool requestFlag = false;
bool extraDiscoverNeeded = false;
uint8_t failedDiscovers = 0;
uint8_t failedRequests = 0;
bool arpAllClear = false;
bool conflictResolutionMode = false;

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

typedef struct _dhcpLocal
{
    uint8_t offeredAddr[4];
    uint8_t serverIp[4];
    uint32_t leaseTotal;
	uint32_t leaseT1;
	uint32_t leaseT2;
} dhcpLocal;
dhcpLocal localInfo = {0};

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void displayLocalInfo()
{
	uint8_t i;
	char str[10];
	
	putsUart0("\n-Local DHCP Info-\n\n");
	
	putsUart0("  Offr:    ");
	for(i = 0; i < IP_ADD_LENGTH; i++)
	{
		sprintf(str, "%u", localInfo.offeredAddr[i]);
		putsUart0(str);
		if (i < 4-1)
			putcUart0('.');
	}
	
	putsUart0("\n SvrIP:    ");
	for(i = 0; i < IP_ADD_LENGTH; i++)
	{
		sprintf(str, "%u", localInfo.serverIp[i]);
		putsUart0(str);
		if (i < 4-1)
			putcUart0('.');
	}
	
	putsUart0("\n Lease:    ");
	sprintf(str, "%u", dhcpGetLeaseSeconds() );
	putsUart0(str);
	
	putsUart0("\n LeaseT1:    ");
	sprintf(str, "%u", localInfo.leaseT1 );
	putsUart0(str);
	
	putsUart0("\n LeaseT2:    ");
	sprintf(str, "%u", localInfo.leaseT2 );
	putsUart0(str);
	
	putcUart0('\n');
}


// State functions

void dhcpSetState(uint8_t state)
{
	// ignore if trying to change state to the current state
	if(dhcpState == state)
		return;
	
    dhcpState = state;
	putsUart0("\nDHCP State set to: ");
	putcUart0(state + 48);
	putsUart0("\n\n");
}

uint8_t dhcpGetState()
{
    return dhcpState;
}


//-----------------------------------------------------------------------------
// Timer Interrupt Functions
//-----------------------------------------------------------------------------

void discoveryTimeout()
{
    extraDiscoverNeeded = true;
	failedDiscovers++;
	if(failedDiscovers == MAX_FAILED_DISCOVERS)
	{
		putsUart0("2 failed DHCPDISCOVER msgs.\nRebooting System...\n\n");
		rebootSystem();
	}
    dhcpSetState(DHCP_INIT);
}

void requestTimeout()
{
	requestFlag = true;
	failedRequests++;
	if(failedRequests == MAX_FAILED_REQUESTS)
	{
		putsUart0("4 failed DHCPREQUEST msgs.\nRestarting DHCP Process...\n\n");
		dhcpSetState(DHCP_INIT);
	}
}

void renewReqTimeout()
{
	
}

void rebindReqTimeout()
{
	
}

// The Lease has expired
void leaseTimeout()
{
	putsUart0("Total lease time expired!\nDisabling DHCP and rebooting...\n\n");
	dhcpSetState(DHCP_DISABLED);
	rebootSystem();
}

void t1Timeout()
{
	renewFlag = true;
	//dhcpSetState(DHCP_RENEWING);
}

void t2Timeout()
{
	rebindFlag = true;
	//dhcpSetState(DHCP_REBINDING);
}

void arpTimeout()
{
	arpAllClear = true;
	conflictResolutionMode = false;
}

bool sendTestARP = false;
void testARP()
{
	sendTestARP = true;
}

bool stopAllTimers()
{
	bool found = false;
	found = found | stopTimer(discoveryTimeout);
	found = found | stopTimer(requestTimeout);
	found = found | stopTimer(leaseTimeout);
	found = found | stopTimer(t1Timeout);
	found = found | stopTimer(t2Timeout);
	found = found | stopTimer(arpTimeout);
	return found;
}

void clearAllFlags()
{
	renewFlag = false;
	rebindFlag = false;
	releaseFlag = false;
	requestFlag = false;
	extraDiscoverNeeded = false;
	arpAllClear = false;
}

// Send DHCP message
// Padding: pad to two bytes?
void dhcpSendMessage(etherHeader *ether, uint8_t type)
{
    uint32_t sum = 0;
    uint8_t i, opt, ipHeaderLength;
    uint16_t tmp16, dhcpSize;
    uint8_t mac[6];

    // Ether frame
    etherGetMacAddress(mac);
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = 0xFF;
        ether->sourceAddress[i] = mac[i];
    }
    ether->frameType = htons(0x800);

    // IP header
    ipHeader* ip = (ipHeader*)ether->data;
    ip->revSize = 0x45;
    ipHeaderLength = (ip->revSize & 0xF) * 4;
    ip->typeOfService = 0;
    ip->id = 0;
    ip->flagsAndOffset = 0;
    ip->ttl = 128;
    ip->protocol = 17;
    ip->headerChecksum = 0;
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        ip->destIp[i] = 0xFF;
        ip->sourceIp[i] = 0x0;
    }

    // UDP header
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    udp->sourcePort = htons(68);
    udp->destPort = htons(67);

    // DHCP
    dhcpFrame* dhcp = (dhcpFrame*)udp->data;
    dhcp->op = 1; // Request, always sending messages, should always be 1?
	dhcp->htype = 1; // 10 mb Ethernet
	dhcp->hlen = HW_ADD_LENGTH; // 10 mb Ethernet H
	dhcp->hops = 0; // Client resets

	if(type == DHCPDISCOVER)
	    local_xid = random32();
	//rand() % (1 << 30);
	dhcp->xid = htonl(local_xid);
	
	dhcp->secs = htons(0x0000);
	
	// Unicast conversation only happens on a RENEW
	if(dhcpGetState() == DHCP_RENEWING)
		dhcp->flags = htons(0x0000); // BROADCAST Bit = 1, rest 0s | Indicates what the response to this message is.
	else
		dhcp->flags = htons(0x8000);
	
	// if rebinding, try to rebind with any dhcp server
	if(dhcpGetState() == DHCP_REBINDING)
		for(i = 0; i < IP_ADD_LENGTH; i++)
			ip->destIp[i] = 0xFF;
	
	// if we have an IP now, fill in the source IP as that
	uint8_t myIP[4];
	if(dhcpGetState() == DHCP_BOUND || dhcpGetState() == DHCP_RENEWING || dhcpGetState() == DHCP_REBINDING)
	{
		etherGetIpAddress(myIP);
		for(i = 0; i < IP_ADD_LENGTH; i++)
		{
			ip->sourceIp[i] = myIP[i];
		}
	}
	
	// Unicast to the server if we are renewing or bound
	if(dhcpGetState() == DHCP_BOUND || dhcpGetState() == DHCP_RENEWING)
		for(i = 0; i < IP_ADD_LENGTH; i++)
			ip->destIp[i] = localInfo.serverIp[i];
	
	// if discovering, clear 'your' and client fields
	// if all clear received, fill in fields with IP Address
	for(i = 0; i < IP_ADD_LENGTH; i++)
	{
		if(type == DHCPDISCOVER)
		{
			dhcp->ciaddr[i] = '\0';
			dhcp->yiaddr[i] = '\0';
		}
		else if(arpAllClear)
		{
			dhcp->ciaddr[i] = localInfo.offeredAddr[i];
			dhcp->yiaddr[i] = localInfo.offeredAddr[i];
			//dhcp->yiaddr[i] = '\0';
		}
	}
	
	// Pretty sure these are always 0
	for(i = 0; i < IP_ADD_LENGTH; i++)
	{
	    dhcp->siaddr[i] = '\0';
	    dhcp->giaddr[i] = '\0';
	}

	// Fill in MAC Address and 0 pad the rest
	for(i = 0; i < HW_ADD_LENGTH; i++)
		dhcp->chaddr[i] = mac[i];
	for(; i < 16; i++)
	    dhcp->chaddr[i] = 0;
	
	// clears file boot and sname
	dhcp->data[0] = '\0';
	dhcp->data[64] = '\0';
	
	// If I need to pad the entire data field
	/* for(i = 0; i < sizeof(dhcp->data); i++)
	    dhcp->data[i] = '\0'; */
	
	dhcp->magicCookie = htonl(MAGIC_COOKIE); // 63.82.53.63
	opt = 0;
	
	// Message Type, required in every DHCP Message
	dhcp->options[opt++] = 53; // Message Type
	dhcp->options[opt++] = 1; // Length
	dhcp->options[opt++] = type;

	if(1) //type == DHCPDISCOVER)
	{
		// Paramter Request List
		dhcp->options[opt++] = 55; // Parameter Request List
		dhcp->options[opt++] = 5; // Number of options
		dhcp->options[opt++] = 1; // Subnet Mask
		dhcp->options[opt++] = 3; // Router Server
		dhcp->options[opt++] = 6; // Domain Name Server
		dhcp->options[opt++] = 58; // T1 Renewal
		dhcp->options[opt++] = 59; // T2 Binding
		// 55, # of options, o1, o2, ...
	}
	
	
	// Client Identifier for RENEWs
	if(dhcpGetState() == DHCP_RENEWING)
	{
		dhcp->options[opt++] = 61;
		dhcp->options[opt++] = 7;
		dhcp->options[opt++] = 1;
		for(i = 0; i < HW_ADD_LENGTH; i++)
		{
			dhcp->options[opt++] = mac[i];
		}
	}


	// Fill in the server identifier option in specific instances
	if(dhcpGetState() == DHCP_SELECTING || type == DHCPDECLINE || type == DHCPRELEASE)
	{
		dhcp->options[opt++] = 54; // Server Identifier
		dhcp->options[opt++] = 4;
		for(i = 0; i < IP_ADD_LENGTH; i++)
			dhcp->options[opt++] = localInfo.serverIp[i];
	}

	// Fill in the Requested IP Address option in specific instances
	if(dhcpGetState() == DHCP_SELECTING || type == DHCPDECLINE)
	{
		dhcp->options[opt++] = 50; // Requested IP Address
		dhcp->options[opt++] = 4;
		for(i = 0; i < IP_ADD_LENGTH; i++)
			dhcp->options[opt++] = localInfo.offeredAddr[i];
	}
	
	// end of option field
	dhcp->options[opt++] = 255;

	// calculate dhcp size, update ip and udp lengths
	dhcpSize = sizeof(dhcpFrame) + opt; // sizeof(dhcpFrame) includes magicCookie | 240 + options
	uint16_t udpLength = sizeof(udpHeader) + dhcpSize;
	udp->length = htons(udpLength);
	ip->length = htons(udpLength + ipHeaderLength);

    // fully calculate and store ip header checksum
	etherCalcIpChecksum(ip);
	
	// psuedo-header
	etherSumWords(ip->sourceIp, 8, &sum);  // adds source and dest
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8; // should have '17' in upper 8 bits, 00 in lower 8 bits
    etherSumWords(&udp->length, 2, &sum);  // adds length (2 bytes)
	
    // add udp header with data, udpLength includes dhcpSize
    udp->check = 0;
    etherSumWords(udp, udpLength, &sum);
	
	// store udp checksum
	udp->check = getEtherChecksum(sum);
	
    // send packet with size = ether hdr + ip header + udp hdr + dhcp_size
    // send packet
	etherPutPacket(ether, sizeof(etherHeader) + ipHeaderLength + udpLength);
	putsUart0("Sent DHCP Message: ");
	putcUart0(type + 48);
	putcUart0('\n');
}

// length* is for storing the length after found as some options are variable length
uint8_t* getOption(etherHeader *ether, uint8_t option, uint8_t* length)
{
    ipHeader* ip = (ipHeader*)ether->data;
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    dhcpFrame* dhcp = (dhcpFrame*)&udp->data;
    // suggest this function to programatically extract the field pointer and length
	uint8_t opt = 0;
	uint16_t dhcpSize = ntohs(udp->length) - sizeof(udpHeader) - sizeof(dhcpFrame); // Options length
	bool foundOpt = false;
	uint8_t isOpt = 0;
	
	char err[4];
	
	// look at each option type, check if its the one we want
	// if we don't find it, jump to the next byte, get the length and increment by that many
	while(foundOpt == false && isOpt != 255)
	{
	    isOpt = dhcp->options[opt];
		if(isOpt == option)
			foundOpt = true;
		else if(!foundOpt)
			opt += dhcp->options[++opt] + 1;
	}
	// if we do find the option we want, send back the length (if requested)
	// and send back a pointer to the beginning of the option's data
	if(foundOpt)
	{
	    if(length != NULL)
	        *length = dhcp->options[opt] + 1;

	    uint8_t * returnVal = &(dhcp->options[opt]) + 2; // to return the start of the values, no option and length
	    return returnVal;
	}
	else
	{
		putsUart0("ERR: Requested option: ");
		sprintf(err, "%u", option);
		putsUart0(err);
		putsUart0(" not found!\n");
		return NULL;
	}
}

// Determines whether packet is DHCP offer response to DHCP discover
// Must be a UDP packet
// ipOfferedAdd is where you store what you get from the server, not a value we send in
bool dhcpIsOffer(etherHeader *ether, uint8_t ipOfferedAdd[])
{
    ipHeader* ip = (ipHeader*)ether->data;
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    dhcpFrame* dhcp = (dhcpFrame*)&udp->data;
	uint8_t * offrServer;
	

    // return true if destport=68 and sourceport=67, op=2, xid correct, and offer msg
	// get the 55 option
	
    bool ok = false;
	ok = (udp->sourcePort == htons(67)) & (udp->destPort == htons(68));
	ok = ok & (dhcp->op == 2);
	ok = ok & (local_xid == htonl(dhcp->xid));
	ok = ok & (*getOption(ether, 53, NULL) == DHCPOFFER);

	uint8_t i;
	if(ok)
	{
		offrServer = getOption(ether, 54, NULL);
		
	    for(i = 0; i < IP_ADD_LENGTH; i++)
		{
	        ipOfferedAdd[i] = dhcp->yiaddr[i];
			localInfo.serverIp[i] = offrServer[i];
		}
	}
    return ok;
}

// Determines whether packet is DHCP ACK response to DHCP request
// Must be a UDP packet
// only after this true is when we store IP locally
bool dhcpIsAck(etherHeader *ether)
{
    ipHeader* ip = (ipHeader*)ether->data;
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    dhcpFrame* dhcp = (dhcpFrame*)&udp->data;
    // return true if destport=68 and sourceport=67, op=2, xid correct, and ack msg
    bool ok = false;
    ok = (udp->sourcePort == htons(67)) & (udp->destPort == htons(68));
    ok = ok & (dhcp->op == 2);
    ok = ok & (local_xid == htonl(dhcp->xid));
    ok = ok & (*getOption(ether, 53, NULL) == DHCPACK);
	
	if(ok)
		failedRequests = 0;
	
    return ok;
}

// Handle a DHCP ACK
void dhcpHandleAck(etherHeader *ether)
{
    ipHeader* ip = (ipHeader*)ether->data;
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    dhcpFrame* dhcp = (dhcpFrame*)&udp->data;
	
    uint8_t i;
    uint8_t * optInfo = NULL;
    uint32_t * leaseInfo = NULL;
	
    // extract offered IP address
    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
		// use set functions from eth0.c
        localInfo.offeredAddr[i] = dhcp->yiaddr[i];
		
        optInfo = getOption(ether, 54, NULL);
        localInfo.serverIp[i] = optInfo[i];
		
		optInfo = getOption(ether, 1, NULL);
		etherSetIpSubnetMask(optInfo);
		
		optInfo = getOption(ether, 3, NULL);
		etherSetIpGatewayAddress(optInfo);
		
		optInfo = getOption(ether, 6, NULL);
		etherSetIpDnsAddress(optInfo);
    }
	
	// store lease, t1, and t2
    leaseInfo = (uint32_t *)getOption(ether, 51, NULL);
	if(leaseInfo != NULL)
		localInfo.leaseTotal = ntohl(*leaseInfo);
	//else panic
	
	leaseInfo = (uint32_t *)getOption(ether, 58, NULL);
	if(leaseInfo != NULL)
		localInfo.leaseT1 = ntohl(*leaseInfo);
	else
		localInfo.leaseT1 = localInfo.leaseTotal >> 1;
	
	leaseInfo = (uint32_t *)getOption(ether, 59, NULL);
	if(leaseInfo != NULL)
		localInfo.leaseT2 = ntohl(*leaseInfo);
	else
		localInfo.leaseT2 = localInfo.leaseTotal * .875;
 
    // stop new address needed timer, t1 timer, t2 timer
	
    // start t1, t2, and lease end timers
	
}

void dhcpSendPendingMessages(etherHeader *ether)
{
    ipHeader* ip = (ipHeader*)ether->data;
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    dhcpFrame* dhcp = (dhcpFrame*)&udp->data;
	
	uint8_t i;


	if(sendTestARP)
	{
		etherSendArpRequest(ether, localInfo.offeredAddr, localInfo.offeredAddr);
		sendTestARP = false;
	}

    // if discover needed, send discover, enter selecting state
    if(dhcpGetState() == DHCP_INIT)
    {
        dhcpSendMessage(ether, DHCPDISCOVER);
		dhcpSetState(DHCP_SELECTING);

		// start discovery timeout
		if(!extraDiscoverNeeded && !restartTimer(discoveryTimeout))
			startPeriodicTimer(discoveryTimeout, 5);
    }
	
	else if(releaseFlag)
    {
        dhcpSendMessage(ether, DHCPRELEASE);
        releaseFlag = false;
		stopAllTimers();
		
		uint8_t zeroIP[4] = {0,0,0,0};
		etherSetIpAddress(zeroIP);
		
    }
	
	else if(renewFlag)
	{
		dhcpSetState(DHCP_RENEWING);
		dhcpSendMessage(ether, DHCPREQUEST);
		restartTimer(requestTimeout);
		renewFlag = false;
		requestFlag = false;
		// TODO
	}
	
	else if(rebindFlag)
	{
		dhcpSetState(DHCP_REBINDING);
		dhcpSendMessage(ether, DHCPREQUEST);
		restartTimer(requestTimeout);
		rebindFlag = false;
		requestFlag = false;
		// TODO
	}
	
	//
	else if(requestFlag)
	{
		if(dhcpGetState() == DHCP_SELECTING)
		{
			dhcpSendMessage(ether, DHCPREQUEST);
			dhcpSetState(DHCP_REQUESTING);
		}
		
		if(dhcpGetState() == DHCP_RENEWING)
			dhcpSendMessage(ether, DHCPREQUEST);
		
		if(dhcpGetState() == DHCP_REBINDING)
			dhcpSendMessage(ether, DHCPREQUEST);
		
		// start request timer
		if(!restartTimer(requestTimeout))
			startPeriodicTimer(requestTimeout, 5);
		
		requestFlag = false;
	}
	
	// if the ARP timeout occured
	// and we do not currently have a filled in IP Addr
	// and we are coming from the TESTING state
	// 	then start using the offered IP Address and become BOUND
	if( arpAllClear && dhcpGetState() == DHCP_TESTING_IP )
	{
		putsUart0("Received all clear! Setting IP Address.\n");
		for(i = 0; i < IP_ADD_LENGTH; i++)
		{
			//dhcp->ciaddr[i] = localInfo.offeredAddr[i];
			etherSetIpAddress(localInfo.offeredAddr);
		}
		dhcpSetState(DHCP_BOUND);
		
		if( !restartTimer(leaseTimeout) || !restartTimer(t1Timeout) || !restartTimer(t2Timeout) )
		{
			/* startOneshotTimer(leaseTimeout, localInfo.leaseTotal);
			startOneshotTimer(t1Timeout, localInfo.leaseT1);
			startOneshotTimer(t2Timeout, localInfo.leaseT2); */
			//startOneshotTimer(leaseTimeout, localInfo.leaseTotal / 1000);
			//startOneshotTimer(t1Timeout, localInfo.leaseT1 / 1000);
			startOneshotTimer(t1Timeout, 5);
			//startOneshotTimer(t2Timeout, localInfo.leaseT2 / 1000);
			startOneshotTimer(t2Timeout, 10);
		}
	}
}

void dhcpProcessDhcpResponse(etherHeader *ether)
{
	ipHeader* ip = (ipHeader*)ether->data;
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    dhcpFrame* dhcp = (dhcpFrame*)&udp->data;
	
    //char optInfo[255];
	uint8_t optionLength, i;

    uint8_t *opt = NULL;
	//opt = getOption(ether, 6, &optionLength);

    // do some checking here for timeouts with timer
	
    // if offer, send request and enter requesting state
	if(dhcpGetState() == DHCP_SELECTING && dhcpIsOffer(ether, localInfo.offeredAddr))
	{
	    stopTimer(discoveryTimeout);
		extraDiscoverNeeded = false;
		failedDiscovers = 0;
	    putsUart0("Received DHCP offer!\n");
		requestFlag = true;
		// start request timeout timer
	}

	// if ack, call handle ack, send arp request, enter ip conflict test state
	else if(dhcpGetState() == DHCP_REQUESTING && dhcpIsAck(ether))
	{
		// stop request timeout timer
		stopTimer(requestTimeout);
		
	    putsUart0("Received DHCP ACK from initial request!\n");
		
	    dhcpHandleAck(ether);
		// arp request w TESTING IP State
		if(!restartTimer(arpTimeout))
			startOneshotTimer(arpTimeout, 20);
		
		if(!restartTimer(testARP))
			startOneshotTimer(testARP, 10);
		
		//etherSetIpAddress(localInfo.offeredAddr);
		dhcpSetState(DHCP_TESTING_IP);
		conflictResolutionMode = true;
		
		putsUart0("IP to test: ");
		char myIP[10];
		for(i = 0; i < IP_ADD_LENGTH; i++)
		{
			sprintf(myIP, "%u", localInfo.offeredAddr[i]);
			putsUart0(myIP);
			if(i - 4 > 1)
				putcUart0('.');
		}
		putcUart0('\n');
		
		putsUart0("Now testing for offered IP...\n\n");
		etherSendArpRequest(ether, localInfo.offeredAddr, localInfo.offeredAddr);
	}
	
	else if(dhcpGetState() == DHCP_RENEWING && dhcpIsAck(ether))
	{
		stopTimer(requestTimeout);
		
		// Renew all Lease Timers
		restartTimer(leaseTimeout);
		restartTimer(t1Timeout);
		restartTimer(t2Timeout);
		
		dhcpSetState(DHCP_BOUND);
	}
	
	else if(dhcpGetState() == DHCP_REBINDING && dhcpIsAck(ether))
	{
		stopTimer(requestTimeout);
		
		restartTimer(leaseTimeout);
		restartTimer(t1Timeout);
		restartTimer(t2Timeout);
		
		dhcpSetState(DHCP_BOUND);
	}
	// do I need to handle Informs here as well?
}

void dhcpProcessArpResponse(etherHeader *ether)
{	

	arpPacket *arp = (arpPacket*)ether->data;
	
	uint8_t i, match = 0, matchS = 0, matchD = 0;
	char str[10];
	bool selfResponse = false;
	
	putsUart0("Received ARP...\n IP: ");
	for(i = 0; i < IP_ADD_LENGTH; i++)
	{
		sprintf(str, "%u", arp->sourceIp[i]);
		putsUart0(str);
		if (i < 4-1)
			putcUart0('.');
	}
	putcUart0('\n');
	
	putsUart0("MAC: ");
	for(i = 0; i < HW_ADD_LENGTH; i++)
	{
		sprintf(str, "%u", arp->sourceAddress[i]);
		putsUart0(str);
		if (i < 6-1)
			putcUart0(':');
	}
	putcUart0('\n');
	
	if(arpAllClear || !conflictResolutionMode)
		return;
	
	stopTimer(arpTimeout);
	
	// --------------------------------
	// Check for MAC Conflict
	uint8_t mac[6];
	etherGetMacAddress(mac);
	match = 0;
	
	for(i = 0; i < HW_ADD_LENGTH; i++)
		if(arp->sourceAddress[i] == mac[i])
			match++;
		
	// We are responding to our own request, ignore it
	if(match == HW_ADD_LENGTH)
		selfResponse = true;

	// --------------------------------------
	// Check for IP Conflict
	for(i = 0; i < IP_ADD_LENGTH; i++)
	{
		if(localInfo.offeredAddr[i] == arp->sourceIp[i])
			matchS++;
		
		if(localInfo.offeredAddr[i] == arp->destIp[i])
			matchD++;
	}
	
	// IP Conflict found!
	if(matchS == IP_ADD_LENGTH)
	{
		putsUart0("IP Conflict found...");
		if(selfResponse)
		{
			putsUart0("but I responded to myself, so this ARP is ok!\nWaiting for other conflicts...\n");
			restartTimer(arpTimeout);
			return;
		}
		else
		{
			putsUart0("but this isn't from me! Restarting DHCP Process...\n\n");
			dhcpSendMessage(ether, DHCPDECLINE);
			dhcpSetState(DHCP_INIT);
			return;
		}
	}
}

// DHCP control functions

void dhcpEnable()
{
	// Begin requesting new address
	if(!dhcpIsEnabled())
	{
		dhcpSetState(DHCP_INIT);
		putsUart0("DHCP enabled.\n");
	}
	else
		putsUart0("DHCP already enabled.\n");
}

void dhcpDisable()
{
	// stopTimer || stopTimer || ... || stopTimer
	stopAllTimers();
	clearAllFlags();
	
	uint8_t zeroIP[4] = {0, 0, 0, 0};
	
	etherSetIpAddress(zeroIP);
	
    dhcpSetState(DHCP_DISABLED);
	putsUart0("DHCP Disabled.\n");
	
    // set state to disabled, stop all timers
}

bool dhcpIsEnabled()
{
    return (dhcpState != DHCP_DISABLED);
}

void dhcpRequestRenew()
{
	renewFlag = true;
}

void dhcpRequestRebind()
{
	rebindFlag = true;
}

void dhcpRequestRelease()
{
    releaseFlag = true;
}

uint32_t dhcpGetLeaseSeconds()
{
    return localInfo.leaseTotal;
}

