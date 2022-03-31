// DHCP Library
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL w/ ENC28J60
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Hardware configuration:
// ENC28J60 Ethernet controller on SPI0
//   MOSI (SSI0Tx) on PA5
//   MISO (SSI0Rx) on PA4
//   SCLK (SSI0Clk) on PA2
//   ~CS (SW controlled) on PA3
//   WOL on PB3
//   INT on PC6

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#ifndef DHCP_H_
#define DHCP_H_

#include <stdint.h>
#include <stdbool.h>
#include "eth0.h"

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void displayLocalInfo(void);

uint8_t dhcpGetState(void);

void dhcpSendPendingMessages(etherHeader *ether);
void dhcpProcessDhcpResponse(etherHeader *ether);
void dhcpProcessArpResponse(etherHeader *ether);

void dhcpEnable(void);
void dhcpDisable(void);
bool dhcpIsEnabled(void);

void dhcpRequestRenew(void);
void dhcpRequestRelease(void);

uint32_t dhcpGetLeaseSeconds();

#endif

