#pragma once
#include "MemoryTypes.h"

/**
 * @brief source related to ACCORE.IRX MMIO and stuff
 * 
 */


namespace ACCORE {
    u16 Read16(u32 addr);
    void Write16(u32 addr, u16 val);
	
	enum {
		INTRN_ATA = 0x0,
		INTRN_JV = 0x1,
		INTRN_UART = 0x2,
		INTRN_LAST = 0x2,
	};
}

#define ACCORE_INTR_ATA  0xB3000000
#define ACCORE_INTR_UART 0xB3100000