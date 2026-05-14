#pragma once

#include "MemoryTypes.h"
#include "common/Pcsx2Types.h"
#include "common/Pcsx2Defs.h"
#include <string>

#define ACSRAM_ADDR_BASE 0xB2500000
#define ACSRAM_RANGE 0x1250
#define ACSRAM_ADDR_BASE_IOP_POV 0x12500000
#define ACSRAM_MAX_SIZE _32kb // size of the SRAM

namespace ACSRAM
{
    // emulation methods
    u16 Read16(u32 addr);

    void Write16(u32 addr, u16 val);

    // data
    extern u8 buffer[];
    extern std::string filepath;

    //storage
    int ReadFile();
    int WriteFile();
    void Clear(u8 fillerbyte = 0x0);
}
