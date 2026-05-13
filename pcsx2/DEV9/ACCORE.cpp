#include "ACCORE.h"
#include "ACJV.h"
#include "common/Console.h"

#define ANS(addr, what) case addr: return what

u16 ACCORE::Read16(u32 addr) {
    switch (addr) {
    ANS(0x1241C000, 0); // ACRAM will wait 0xFFFFF times for this to not have 0x1000 bitmask set. also used inside intr_intr (interrup handler 13 declared on accore)
    
    break;
    default: Console.Error("%-16s %08X:  %04X", "ACUNK::Read16", addr, 0); return 0;
    }
    return 0;
}

void ACCORE::Write16(u32 mem, u16 value) {
		switch (mem) {
		case ACJV_CTR_START: Console.Warning("ACJV::START"); ACJV::enabled = true; break;
		case ACJV_CTR_STOP:  Console.Warning("ACJV::STOP");  ACJV::enabled = false;  break;
		case 0x1241510C:  Console.Warning("ACCORE::INTR  DISABLE_ACATA_INTR"); break;
		case 0x1241511C:  Console.Warning("ACCORE::INTR  DISABLE_ACUART_INTR"); break;
		// ACFPGA UPLOAD MMIO ///TODOx6: move this handling to ACFPGA.cpp
		case 0x12416012: // set to 0 after ACFPGALD uploads bitstream and during startup of ACRAM
		case 0x12416014:
		case 0x12416018:
		case 0x12416016:
		case 0x1241601A:
			break;
		// unknown addresses set to 0 on ACCORE. most likely stopping other stuff
		//case 0x12416032: break;
		//case 0x12416032: break;
		//case 0x12416036: break;
		//case 0x1241603A: break;
		//case 0x12417000: break;
		//case 0x1241601E: break;

		default: Console.Error("%-16s %08X = %04X", "ACUNK::write16", mem, value); break;
		}
}