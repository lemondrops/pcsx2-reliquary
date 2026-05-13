#pragma once

/**
 * @file ACATA.h
 * ATA interface through the NAMCO board of the SYSTEM246/256
 * used by ACATA.IRX, which is used by ACDVDV and ACATAD for using disc readers or hard drives on system2x6 units
 */

#include "MemoryTypes.h"
#include "common/Pcsx2Types.h"
#include "common/Pcsx2Defs.h"
#include "common/ARCADE.h"
#include <map>

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include "common/RedtapeWindows.h"
#include "common/Path.h"

#define ACATA_DEVCNT             2 // ammount of ATA devices probed by ACATA.IRX

#include "ACATA_internal.h"
#include "ACATAPI.h"

#define ACATA_PROBEREG_0         0x16020000
#define ACATA_PROBEREG_1         0x16030000
#define ACATA_PROBEREG_2         0x16160000 // ALTSTATUS / CONTROL / r:ata dma stat | w: ata flags (see ps2sdk impl) 
#define ACATA_PROBEREG_3         0x16010000 // ERROR / FEATURES
#define ACATA_DEVICE_SELECT      0x16060000 // ACATA_DEVICE_SELECT [`ACATA_UNIT0`, `ACATA_UNIT1`]
#define ACATA_R_STATUS           0x16070000 // STATUS / COMMAND

#define ACATA_16050000           0x16050000 // CYL HIGH?
#define ACATA_16040000           0x16040000 // CYL LOW?

#define ACATA_BASE_PROBEADDR     0x16000000 // DATA
#define ACATA_RANGE              0x1600

#define ACATA_UNIT0              0x0  // 16 * (unitIndex != 0)
#define ACATA_UNIT1              0x10 // 16 * (unitIndex != 0)

#define ACATA_ATACMD_INCOMMING   0x700
#define ACATA_PROBE_BEGIN_NOTICE 0x16020000 // set to 4660

#define ACATA_TRANSF_DMA 0x1 // the requested transfer will be done over DMA, not PIO
#define ACATA_ISDMA (ACATA::REGS[ACATA_PROBEREG_3] & ACATA_TRANSF_DMA) // `*0x16010000 & 1` indicates DMA will be used

#define ACATA_STATUS ACATA::REGS[ACATA_R_STATUS]

namespace ACATA 
{
    extern std::map<u32, u32> REGS;
    extern int device_probes[ACATA_DEVCNT];
    extern int last_device_probed;
    extern u32 last_read;
    extern u32 last_write;
    extern u32 cmd_handled;
    extern u32 cmd_handledc;
    extern atapi_packet_t ata_c_packet;
    extern std::string imgpath;

    u16 read16(u32 addr);               // handle writes to ACATA MMIO
    void write16(u32 addr, u16 val);    // handle reads  to ACATA MMIO
    u16 cmd_handleR(u32 addr);          // handle reads  to 0x16000000 while ATA command
    void cmd_handleW(u32 addr, u16 val); // handle writes to 0x16000000 while ATA command
    void rstat_write_handle(u16 val);   // handle writes to 0x16070000, usually: ATA command requests
    extern ACMEDIATYPE MediaType;
    namespace TH { //dedicated namespace for thread related code
        
        enum PTRNSF {
            NONE = 0,
            ATA,
            ATAPI,
        };
        extern enum PTRNSF PendTrasnfType; //pending transfer type?
	    extern std::mutex ioMutex;
        extern bool b_isIdle,
            ioWrite,
            ioRead;
	    extern std::condition_variable Idle_cv, ioReady;
        extern FILE* IMAGE;
	    extern int readBufferLen;
	    extern u8* readBuffer;
        extern u32 sectorsize; //512 for hdd and 2048 for Disc (?) check it
        extern u32 nsector;
        extern s64 LBA;
        
        void IO_Thread();
        void IO_Read();
        void IO_Read(u32* addr, u32 val); //alternate version to vomit data straight away to a ptr
        int IO_OpenImage();
        int IO_CloseImage();
    }
    void SetEnv(std::string ata_img_path, std::string ata_img_filename, std::string Media);
    void SetImgPath(const char* S);
}
