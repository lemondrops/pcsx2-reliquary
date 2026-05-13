#include "ACUART.h"
#include "common/Console.h"

#define ANS(addr, what) case addr: return what

u16 ACUART::Read16(u32 addr) {
    return 0;
}

void ACUART::Write16(u32 addr, u16 val) {
    switch (addr) {
    case 0xB2418002: break; // this seems to be some sort of reg. set to 0 on module stop, and certain bits applied after writing/reading
    case 0xB2418004: if (val == 7) Console.Warning("ACUART::STOP()"); break;
    }
}