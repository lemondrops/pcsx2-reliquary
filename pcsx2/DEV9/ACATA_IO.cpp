
///////////////// I/O THREAD CODE BELOW ONLY
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/FileSystem.h"

#include "ACATA.h"

#if __POSIX__
#define INVALID_HANDLE_VALUE -1
#include <unistd.h>
#include <fcntl.h>
#endif

std::mutex ACATA::TH::ioMutex;
bool ACATA::TH::b_isIdle,
    ACATA::TH::ioWrite,
    ACATA::TH::ioRead;
std::condition_variable ACATA::TH::Idle_cv, ACATA::TH::ioReady;
FILE* ACATA::TH::IMAGE;
int ACATA::TH::readBufferLen;
u8* ACATA::TH::readBuffer = nullptr;
u32 ACATA::TH::sectorsize = ACATAPI::CONSTANTS::DVD_SECTORSIZE; //TODO: remove hardcode before testing HDD/CD games !
u32 ACATA::TH::nsector;
s64 ACATA::TH::LBA;

enum ACATA::TH::PTRNSF ACATA::TH::PendTrasnfType; //pending transfer type?

void ACATA::TH::IO_Thread() {
	std::unique_lock ioWaitHandle(ioMutex);
	b_isIdle = false;
	ioWaitHandle.unlock();

	while (true)
	{
		ioWaitHandle.lock();
		b_isIdle = true;
		Idle_cv.notify_all();

        // AFAIK arcade games cannot write to their media storage in ANY way...
		ioReady.wait(ioWaitHandle, [&] { return ioRead /*| ioWrite*/; });
		b_isIdle = false;

		int ioType = -1;
		if (ioRead)
			ioType = 0;
		else if (ioWrite)
			ioType = 1;

		ioWaitHandle.unlock();

		IO_Read();
	}
}


void ACATA::TH::IO_Read(u32* addr, u32 size) {
	const s64 lba = ACATA::TH::LBA;
	const u64 pos = lba * ACATA::TH::sectorsize;
	u64 size2 = sectorsize*nsector;
	if (size != (size2)) Console.Error("ACATA::TH::IO_Read> mismatch on request and read...\n%ld vs %ld (sec:%d,lba:%d)",
			 size, (size2), sectorsize, nsector);
	if (FileSystem::FSeek64(IMAGE, pos, SEEK_SET) != 0) {
		Console.ErrorFmt("ACATA:IO_Read: failed to seek pos:{}", pos);
		pxAssert(false);
		abort();
	}

	if (std::fread(addr, sectorsize, nsector, IMAGE) != static_cast<size_t>(nsector)) {
		Console.ErrorFmt("ACATA:IO_Read: size:{} at:{} failed", size2, pos);
		pxAssert(false);
		abort();
	}
	{
		std::lock_guard ioSignallock(ioMutex);
		ioRead = false;
	}
}
void ACATA::TH::IO_Read() {
	const s64 lba = ACATA::TH::LBA;
	const u64 pos = lba * ACATA::TH::sectorsize;

	if (FileSystem::FSeek64(IMAGE, pos, SEEK_SET) != 0 ||
		std::fread(readBuffer, sectorsize, nsector, IMAGE) != static_cast<size_t>(nsector))
	{
		Console.Error("ACATA: File read error");
		pxAssert(false);
		abort();
	}
	{
		std::lock_guard ioSignallock(ioMutex);
		ioRead = false;
	}
}

int ACATA::TH::IO_OpenImage() {
	//ACATA::imgpath = std::string("C:\\Users\\Isra\\OneDrive\\Escritorio\\scII-DVD0D.iso");
    ACATA::TH::IMAGE = std::fopen(ACATA::imgpath.c_str(), "rb");
	if (!ACATA::TH::IMAGE) {
		Console.ErrorFmt("ACATA::OpenImage> fail to fopen '{}' w/ error {} '{}'", ACATA::imgpath, errno, strerror(errno));
		return errno;
	}
	return 0;
}
int ACATA::TH::IO_CloseImage() {
	std::fclose(ACATA::TH::IMAGE);
	return 0;
}