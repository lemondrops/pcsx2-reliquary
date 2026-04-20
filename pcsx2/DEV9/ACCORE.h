#pragma once

/**
 * @brief source related to ACCORE.IRX MMIO and stuff
 * 
 */



enum {
	ACCORE_INTRN_ATA = 0x0,
	ACCORE_INTRN_JV = 0x1,
	ACCORE_INTRN_UART = 0x2,
	ACCORE_INTRN_LAST = 0x2,
};

#define ACCORE_INTR_ATA  0xB3000000
#define ACCORE_INTR_UART 0xB3100000