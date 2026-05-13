#include "ACSRAM.h"
#include "IopMem.h"
#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/StringUtil.h"
#include "ps2/BiosTools.h"

/// TODO: ACSRAM should be saved like PS2 NVRAM!!
/// TODO: ACSRAM should be per-game. keep it per-emulator for the duration of the early development stage

#define OOB_REPORT(T) Console.Error("ACSRAM: out of bound %s: %08X", __FUNCTION__, T);

u8 ACSRAM::buffer[NamcoMemSize::ACSRAM];
u8 compbuf[NamcoMemSize::ACSRAM] = {0};
std::string ACSRAM::filepath = "";
bool LockSave = false; //why? because I dont wanna wipe an SRAM image that already existed but failed to be read


int ACSRAM::ReadFile() {
	Console.WriteLn("%s", __FUNCTION__);
    Error error;
    FILE* FD = std::fopen(ACSRAM::filepath.c_str(), "r+b");
    if (FD) {
        // unlike PS2 NVRAM, which we can do a generic damage/invalid detection attempt. 
        // ACSRAM is per-game, and not all of them strongly check if it has valid data
        // this is even problematic on real hardware, where such games can behave erratically when ran on a machine whose ACSRAM has payload of another game
        if (std::fread(compbuf, sizeof(compbuf), 1, FD) == 1) {
            std::memcpy(ACSRAM::buffer, compbuf, NamcoMemSize::ACSRAM);
            return 1;
        } else {
            Console.ErrorFmt("ACSRAM: Could not read all the data");
            LockSave = true; // file opened but failed to read? forbid from saving 
        }
	    std::fclose(FD);
    } else {
        Console.ErrorFmt("ACSRAM: Could not open input image file {} '{}'", errno, strerror(errno));
    }
    Console.Warning("ACSRAM: failed to read SRAM. Preparing blank buffer");
    ACSRAM::Clear(0x0);
    LockSave = false;
    return 0;
}

int ACSRAM::WriteFile() {
	Console.WriteLn("%s", __FUNCTION__);
    if (LockSave) {
        Console.Warning("ACSRAM: skipping sram save to preserve existing image");
        return 0;
    }
	Error error;
    FILE* FD = std::fopen(ACSRAM::filepath.c_str(), "w+b");
    if (!FD) {
        Console.ErrorFmt("ACSRAM: Could not open output image file {} '{}'", errno, strerror(errno));
        return errno;
    }

	if (std::fread(compbuf, sizeof(compbuf), 1, FD) == 1 &&
		std::memcmp(compbuf, ACSRAM::buffer, NamcoMemSize::ACSRAM) == 0) {
		DEV_LOG("ACSRAM has not changed, skip writing to disk.");
	    std::fclose(FD);
		return 0;
	}

	if (FileSystem::FSeek64(FD, 0, SEEK_SET) == 0 &&
		std::fwrite(ACSRAM::buffer, NamcoMemSize::ACSRAM, 1, FD) == 1) {
		INFO_LOG("ACSRAM saved to {}.", Path::GetFileName(ACSRAM::filepath));
	}
	else
	{
	    std::fclose(FD);
		ERROR_LOG("Failed to save ACSRAM to {}: {}", Path::GetFileName(ACSRAM::filepath), strerror(errno));
        return EIO;
    }
	std::fclose(FD);
    return 0;
}

void ACSRAM::Clear(u8 fillerbyte) {
    std::memset(ACSRAM::buffer, fillerbyte, sizeof(ACSRAM::buffer));
}

u8 ACSRAM::Read8(u32 addr) {
    u32 T = addr - ACSRAM_ADDR_BASE_IOP_POV;
    if (T < ACSRAM_MAX_SIZE) {
        return ACSRAM::buffer[T];
    } else OOB_REPORT(T);
    return 0;
}

u16 ACSRAM::Read16(u32 addr) {
    u32 T = addr - ACSRAM_ADDR_BASE_IOP_POV;
    if (T < ACSRAM_MAX_SIZE) {
        u16* A = (u16*)&ACSRAM::buffer[T];
        return *A;
    } else OOB_REPORT(T);
    return 0;
}

u32 ACSRAM::Read32(u32 addr) {
    u32 T = addr - ACSRAM_ADDR_BASE_IOP_POV;
    if (T < ACSRAM_MAX_SIZE) {
        u32* A = (u32*)&ACSRAM::buffer[T];
        return *A;
    } else OOB_REPORT(T);
    return 0;
}


void ACSRAM::Write8(u32 addr, u8 val) {
    u32 T = addr - ACSRAM_ADDR_BASE_IOP_POV;
    if (T < ACSRAM_MAX_SIZE) {
        ACSRAM::buffer[T] = val;
    } else OOB_REPORT(T);
}

void ACSRAM::Write16(u32 addr, u16 val) {
    u32 T = addr - ACSRAM_ADDR_BASE_IOP_POV;
    if (T < ACSRAM_MAX_SIZE) {
        u16* A = (u16*)&ACSRAM::buffer[T];
        *A = val;
    } else OOB_REPORT(T);
}

 void ACSRAM::Write32(u32 addr, u32 val) {
    u32 T = addr - ACSRAM_ADDR_BASE_IOP_POV;
    if (T < ACSRAM_MAX_SIZE) {
        u32* A = (u32*)&ACSRAM::buffer[T];
        *A = val;
    } else OOB_REPORT(T);
}