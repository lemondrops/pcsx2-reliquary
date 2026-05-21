// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "IopDma.h"
#include "IopMem.h"
#include "R3000A.h"
#include "FW.h"

#include "common/Console.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

static u8 phyregs[16];
s8* fwregs;

namespace
{
	constexpr bool FW_VERBOSE_LOGS = false;
	constexpr u32 FW_BUS_RESET_LOG_LIMIT = 4;
	constexpr u32 FW_DISCOVERY_LOG_LIMIT = 16;
	constexpr u32 FW_CROM_LOG_LIMIT = 80;
	constexpr u32 FW_UBUF_LOG_LIMIT = 32;
	constexpr u32 FW_INTR_LOG_LIMIT = 16;
	constexpr u32 FW_SECTOR_LOG_LIMIT = 64;
	constexpr u32 FW_RUNTIME_LOG_LIMIT = 256;

	constexpr u32 FW_INTR0_DRFR = 0x00000001;
	constexpr u32 FW_INTR0_PBCntR = 0x00000200;
	constexpr u32 FW_INTR0_AckRcvd = 0x00004000;
	constexpr u32 FW_INTR0_URx = 0x00400000;
	constexpr u32 FW_INTR0_PhyRst = 0x20000000;
	constexpr u32 FW_INTR0_PhyRRx = 0x40000000;
	constexpr u32 FW_INTR1_UTD = 0x00000002;

	constexpr u32 FW_CTRL0_Root = 0x00080000;
	constexpr u32 FW_CTRL0_BusIDRst = 0x00800000;

	constexpr u32 PHT_CTRL_ST_EWREQ = 0x00010000;
	constexpr u32 PHT_CTRL_ST_ERREQ = 0x00020000;
	constexpr u32 PHT_CTRL_ST_PHTRst = 0x00200000;
	constexpr u32 PHT_CTRL_ST_EWREQ_ERREQ = PHT_CTRL_ST_EWREQ | PHT_CTRL_ST_ERREQ;
	constexpr u32 DBUF_FIFO_RESET_TX = 0x00008000;
	constexpr u32 DBUF_FIFO_RESET_RX = 0x80000000;

	constexpr u8 PHY_REG01_IBR = 0x40;
	constexpr u8 PHY_REG05_ISBR = 0x40;
	constexpr u8 PHY_REG05_EN_ACCL = 0x02;
	constexpr u8 PHY_REG05_EN_MULTI = 0x01;

	constexpr u32 IEEE1394_TCODE_WRITEQ = 0;
	constexpr u32 IEEE1394_TCODE_WRITEB = 1;
	constexpr u32 IEEE1394_TCODE_WRITE_RESPONSE = 2;
	constexpr u32 IEEE1394_TCODE_READQ = 4;
	constexpr u32 IEEE1394_TCODE_READB = 5;
	constexpr u32 IEEE1394_TCODE_READQ_RESPONSE = 6;

	constexpr u8 PHY_SELF_ID_PACKET = 0xe1;
	constexpr u32 REMOTE_PHY_ID = 0;
	constexpr u32 LOCAL_PHY_ID = 1;
	constexpr u32 KONAMI_RESPONSE_SPEED = 2;
	constexpr u32 KONAMI_ACK_COMPLETE = 1;
	constexpr u32 KONAMI_ACK_PEND = 2;
	constexpr u32 SELF_ID_PORT_NOT_CONNECTED = 1;
	constexpr u32 SELF_ID_PORT_PARENT = 2;
	constexpr u32 SELF_ID_PORT_CHILD = 3;
	constexpr u32 LOCAL_NODE_ID = 0xffc0 | LOCAL_PHY_ID;
	constexpr u32 LOCAL_NODE_ID_REGISTER = (0x3ffu << 22) | (LOCAL_PHY_ID << 16) | 1u;
	constexpr u32 KONAMI_CF_COMMAND_OFFSET = 0x390;
	constexpr u32 KONAMI_ATA_COMMAND_OFFSET = 0x3a0;
	constexpr u32 KONAMI_DALLAS_STATUS_OFFSET = 0x00010000;
	constexpr u32 KONAMI_UART_STATUS_OFFSET = 0x00030000;
	constexpr u32 KONAMI_CF_STATUS_OFFSET = 0x00050000;
	constexpr u32 KONAMI_BBSRAM_STATUS_OFFSET = 0x00080000;
	constexpr u32 KONAMI_BOOTROM_STATUS_OFFSET = 0x00090000;
	constexpr u32 KONAMI_FSCI_STATUS_OFFSET = 0x000a0000;
	constexpr u32 KONAMI_CROM_BASE = 0xf0000400;
	constexpr u32 KONAMI_BOOT_READY_OFFSET_HIGH = 0xfffd;
	constexpr u32 KONAMI_BOOT_READY_OFFSET_LOW = 0x05735734;
	constexpr u32 KONAMI_RUNTIME_READY_OFFSET_LOW = 0x05735730;
	constexpr u32 SECTOR_SIZE = 0x200;
	constexpr u32 BOOTROM_SIZE = 0x10000;
	constexpr u32 BBSRAM_SIZE = 0x2000;
	constexpr const char* WE2K3_BLOB_PATH = "";
	constexpr const char* WE2K3_BBSRAM_PATH = "";
	// EE byte-swaps the JAMMA word, then maps source bit 8 to P1 bit 0x200.
	constexpr u32 JAMMA_P1_JVS_PRESENT = 0x00010000;
	// Captured neutral JAMMA status/DIP bytes. The present signal is in the input word above.
	constexpr u32 JAMMA_STATUS_NEUTRAL = 0x0100ffff;
	constexpr u8 KONAMI_FACTORY_MAC[] = {
		0x00, 0x04, 0x5f, 0x00, 0x00, 0x01,
	};
	constexpr u8 KONAMI_MAC_BACKUP[] = {
		0x00, 0x04, 0x5f, 0x00, 0x00, 0x01,
		0xff, 0xfb, 0xa0, 0xff, 0xff, 0xfe,
	};
	constexpr u32 KONAMI_DALLAS_NO_KEY_RESPONSE[] = {
		0x01000000, 0x00000000, 0x00000000,
	};
	constexpr u8 KONAMI_FSCI_SERIAL_STREAM[] =
		"@0DM00045F000001AF"
		"@0DM00045F000001AF"
		"@0DM00045F000001AF";

	struct PendingDbufDmaWrite
	{
		bool active = false;
		u32 dest = 0;
		std::vector<u8> data;
	};

	// Captured from the WE2K3 Python1 IO board's config-ROM reads in python1-boot-ioerror.nosy.
	constexpr u32 KONAMI_IO_BOARD_CROM[] = {
		0x0404a1a4, 0x31333934, 0x407d8002, 0x00000000, 0x00000000, 0x00053f04,
		0x03000679, 0x8100000a, 0x0c0083c0, 0xc3000005, 0xd1000001, 0x00020901,
		0x12000679, 0x13001000, 0x000231e6, 0x17000000, 0x81000006, 0x0004f2d6,
		0x00000000, 0x00000000, 0x4b4f4e41, 0x4d490000, 0x000428a6, 0x00000000,
		0x00000000, 0x5053322d, 0x41430000,
	};

	std::vector<u32> s_ubuf_rx_fifo;
	std::vector<u32> s_ubuf_tx_fifo;
	std::vector<u32> s_pending_dbuf_r0_rx_fifo;
	std::vector<PendingDbufDmaWrite> s_pending_dbuf_r0_rx_dma;
	std::vector<u32> s_dbuf_r0_rx_fifo;
	u8 s_bootrom[BOOTROM_SIZE];
	u8 s_bbsram[BBSRAM_SIZE];
	bool s_bbsram_dirty;
	std::vector<u32> s_pht_tx_fifo[2];
	u32 s_pht_tx_expected_bytes[2];
	bool s_pht_write_pending[2];
	u32 s_bus_reset_log_count;
	u32 s_discovery_log_count;
	u32 s_crom_log_count;
	u32 s_dbuf_log_count;
	u32 s_ubuf_log_count;
	u32 s_intr_log_count;
	u32 s_sector_read_log_count;
	u32 s_pht_log_count;
	u32 s_runtime_log_count;
#ifndef _WIN32
	void WriteTraceErrorMarker(const char* message, int error)
	{
		const char* error_path = std::getenv("PCSX2_FW_TRACE_ERROR_FILE");
		if (!error_path || !error_path[0])
			return;

		const int error_fd = ::open(error_path, O_CREAT | O_WRONLY | O_APPEND, 0644);
		if (error_fd < 0)
			return;

		char error_buffer[128];
		const int error_len = std::snprintf(error_buffer, sizeof(error_buffer), "%s error=%d\n", message, error);
		if (error_len > 0)
			(void)::write(error_fd, error_buffer, static_cast<size_t>(error_len));
		::close(error_fd);
	}
#endif

	void FwTrace(const char* format, ...)
	{
		char buffer[512];
		int offset = 0;
		std::va_list ap;
		va_start(ap, format);
		offset = std::vsnprintf(buffer, sizeof(buffer), format, ap);
		va_end(ap);
		if (offset < 0)
			return;
		if (offset >= static_cast<int>(sizeof(buffer)))
			offset = static_cast<int>(sizeof(buffer)) - 1;
		buffer[offset++] = '\n';

#ifndef _WIN32
		const char* trace_path = std::getenv("PCSX2_FW_TRACE_FILE");
		if (!trace_path || !trace_path[0])
			return;

		const int trace_fd = open(trace_path, O_CREAT | O_WRONLY | O_APPEND, 0644);
		if (trace_fd < 0)
		{
			WriteTraceErrorMarker("open", errno);
			return;
		}

		const ssize_t written = ::write(trace_fd, buffer, offset);
		if (written < 0)
			WriteTraceErrorMarker("write", errno);
		::close(trace_fd);
#endif
	}

	void TraceIopBuffer(const char* prefix, u32 address, u32 byte_count)
	{
		if (byte_count == 0)
			return;

		if (byte_count > 0x40)
			byte_count = 0x40;

		u8 data[0x40] = {};
		const bool ok = iopMemSafeReadBytes(address, data, byte_count);
		u32 words[4] = {};
		for (u32 word = 0; word < std::min<u32>(4, (byte_count + 3) / sizeof(u32)); word++)
		{
			for (u32 i = 0; i < sizeof(u32) && word * sizeof(u32) + i < byte_count; i++)
				words[word] |= static_cast<u32>(data[word * sizeof(u32) + i]) << (24 - i * 8);
		}

		FwTrace("%s addr=0x%x bytes=0x%x ok=%u data=%08x %08x %08x %08x", prefix, address, byte_count, ok ? 1 : 0, words[0], words[1], words[2], words[3]);
	}

	void TraceBytes(const char* prefix, u32 offset, const u8* data, u32 byte_count)
	{
		if (byte_count == 0)
			return;

		const u32 trace_count = std::min<u32>(byte_count, 0x40);
		u32 words[4] = {};
		for (u32 word = 0; word < std::min<u32>(4, (trace_count + 3) / sizeof(u32)); word++)
		{
			for (u32 i = 0; i < sizeof(u32) && word * sizeof(u32) + i < trace_count; i++)
				words[word] |= static_cast<u32>(data[word * sizeof(u32) + i]) << (24 - i * 8);
		}

		FwTrace("%s offset=0x%x bytes=0x%x data=%08x %08x %08x %08x", prefix, offset, byte_count, words[0], words[1], words[2], words[3]);
	}

	const char* GetBbsramPath()
	{
		const char* path = std::getenv("PCSX2_FW_BBSRAM_FILE");
		return (path && path[0]) ? path : WE2K3_BBSRAM_PATH;
	}

	void LoadBbsram()
	{
		std::memset(s_bbsram, 0, sizeof(s_bbsram));
		s_bbsram_dirty = false;

		const char* path = GetBbsramPath();
		std::FILE* file = std::fopen(path, "rb");
		if (!file)
		{
			FwTrace("bbsram load path=%s bytes=0 ok=0", path);
			return;
		}

		const size_t read = std::fread(s_bbsram, 1, sizeof(s_bbsram), file);
		std::fclose(file);
		FwTrace("bbsram load path=%s bytes=0x%zx ok=%u", path, read, read == sizeof(s_bbsram) ? 1 : 0);
		TraceBytes("bbsram load data", 0, s_bbsram, std::min<u32>(0x40, static_cast<u32>(read)));
	}

	void SaveBbsramIfDirty()
	{
		if (!s_bbsram_dirty)
			return;

		const char* path = GetBbsramPath();
		std::FILE* file = std::fopen(path, "wb");
		if (!file)
		{
			FwTrace("bbsram save path=%s bytes=0 ok=0", path);
			return;
		}

		const size_t written = std::fwrite(s_bbsram, 1, sizeof(s_bbsram), file);
		std::fclose(file);
		FwTrace("bbsram save path=%s bytes=0x%zx ok=%u", path, written, written == sizeof(s_bbsram) ? 1 : 0);
		if (written == sizeof(s_bbsram))
			s_bbsram_dirty = false;
	}

	bool ShouldLogLimited(u32& counter, u32 limit)
	{
		counter++;
		return counter <= limit;
	}

	u32 ByteSwap32(u32 value)
	{
		return (value >> 24) | ((value >> 8) & 0x0000ff00) | ((value & 0x0000ff00) << 8) | (value << 24);
	}

	const char* TcodeName(u32 tcode)
	{
		switch (tcode)
		{
			case IEEE1394_TCODE_WRITEQ:
				return "WRITEQ";
			case IEEE1394_TCODE_WRITEB:
				return "WRITEB";
			case IEEE1394_TCODE_WRITE_RESPONSE:
				return "WRITE_RESPONSE";
			case IEEE1394_TCODE_READQ:
				return "READQ";
			case IEEE1394_TCODE_READB:
				return "READB";
			case IEEE1394_TCODE_READQ_RESPONSE:
				return "READQ_RESPONSE";
			default:
				return "UNKNOWN";
		}
	}

	void UpdateUbufRxLevel()
	{
		fwRu32(0x8454) = static_cast<u32>(s_ubuf_rx_fifo.size());
	}

	void UpdateDbufR0RxLevel()
	{
		fwRu32(0x84c0) = static_cast<u32>((s_dbuf_r0_rx_fifo.size() * sizeof(u32)) << 16);
	}

	void RaiseIntr0(u32 bits)
	{
		fwRu32(0x8420) |= bits;
		if (ShouldLogLimited(s_intr_log_count, FW_INTR_LOG_LIMIT))
			DevCon.WriteLn("FW HLE: intr0 raise bits=0x%x intr0=0x%x mask=0x%x", bits, fwRu32(0x8420), fwRu32(0x8424));
		if (fwRu32(0x8424) & bits)
			fwIrq();
	}

	void RaiseIntr1(u32 bits)
	{
		fwRu32(0x8428) |= bits;
		if (fwRu32(0x842c) & bits)
			fwIrq();
	}

	void QueueUbufRx(u32 value)
	{
		s_ubuf_rx_fifo.push_back(value);
		UpdateUbufRxLevel();
		DevCon.WriteLn("FW HLE: UBUF rx queue value=0x%x level=%zu", value, s_ubuf_rx_fifo.size());
	}

	u32 PopUbufRx()
	{
		if (s_ubuf_rx_fifo.empty())
			return 0;

		u32 value = s_ubuf_rx_fifo.front();
		s_ubuf_rx_fifo.erase(s_ubuf_rx_fifo.begin());
		UpdateUbufRxLevel();
		if (ShouldLogLimited(s_ubuf_log_count, FW_UBUF_LOG_LIMIT))
			DevCon.WriteLn("FW HLE: UBUF rx pop value=0x%x remaining=%zu", value, s_ubuf_rx_fifo.size());
		return value;
	}

	void QueueDbufR0Rx(u32 value)
	{
		s_dbuf_r0_rx_fifo.push_back(value);
		UpdateDbufR0RxLevel();
	}

	void FlushPendingDbufR0RxPacket();

	u32 PopDbufR0Rx()
	{
		if (s_dbuf_r0_rx_fifo.empty())
			return 0;

		u32 value = s_dbuf_r0_rx_fifo.front();
		s_dbuf_r0_rx_fifo.erase(s_dbuf_r0_rx_fifo.begin());
		UpdateDbufR0RxLevel();
		const bool became_empty = s_dbuf_r0_rx_fifo.empty();
		if (became_empty)
		{
			DevCon.WriteLn("FW HLE: DBUF R0 rx pop value=0x%x remaining=%zu", value, s_dbuf_r0_rx_fifo.size());
		}
		if (became_empty)
			FlushPendingDbufR0RxPacket();
		return value;
	}

	u8 ReadPhyRegister(u8 reg)
	{
		if (reg == 0x08 && ((phyregs[0x07] >> 5) & 0x7) == 0)
		{
			const u8 port = phyregs[0x07] & 0xf;
			// One connected child port is enough for the Konami i.Link stack to leave discovery.
			return (port == 0) ? 0xae : 0x00;
		}

		if (reg == 0x09 && ((phyregs[0x07] >> 5) & 0x7) == 0)
			return ((phyregs[0x07] & 0xf) == 0) ? 0x40 : 0x00;

		return phyregs[reg];
	}

	void InitializePhyRegisters()
	{
		memset(phyregs, 0, sizeof(phyregs));
		phyregs[0x00] = static_cast<u8>((LOCAL_PHY_ID << 2) | 0x03); // physical ID 0, root, powered.
		phyregs[0x01] = 0x3f;                                      // normal gap count.
		phyregs[0x02] = 0x03;                                      // expose three PHY ports.
		phyregs[0x03] = 0x40;                                      // S400 max speed.
		phyregs[0x04] = 0x80;                                      // link-active report control.
		phyregs[0x05] = PHY_REG05_EN_ACCL | PHY_REG05_EN_MULTI;
	}

	u32 BuildSelfIdQuad(u32 phy_id, u32 port0, u32 port1, u32 port2)
	{
		return 0x80000000u | (phy_id << 24) | (1u << 22) | (0x3fu << 16) | (2u << 14) | (4u << 8) |
			(port0 << 6) | (port1 << 4) | (port2 << 2);
	}

	void QueueSelfIdPacket()
	{
		QueueDbufR0Rx(PHY_SELF_ID_PACKET);
		QueueDbufR0Rx(BuildSelfIdQuad(REMOTE_PHY_ID, SELF_ID_PORT_PARENT, 0, 0));
		QueueDbufR0Rx(BuildSelfIdQuad(LOCAL_PHY_ID, SELF_ID_PORT_CHILD, SELF_ID_PORT_NOT_CONNECTED, SELF_ID_PORT_NOT_CONNECTED));
		QueueDbufR0Rx(1); // End marker consumed by Konami's self-ID parser.
	}

	void TriggerBusReset()
	{
		phyregs[0x00] = static_cast<u8>((LOCAL_PHY_ID << 2) | 0x03);
		fwRu32(0x8400) = LOCAL_NODE_ID_REGISTER;
		fwRu32(0x8408) = (fwRu32(0x8408) | FW_CTRL0_Root) & ~FW_CTRL0_BusIDRst;

		s_ubuf_rx_fifo.clear();
		s_dbuf_r0_rx_fifo.clear();
		QueueSelfIdPacket();
		RaiseIntr0(FW_INTR0_PhyRst | FW_INTR0_DRFR);
		if (ShouldLogLimited(s_bus_reset_log_count, FW_BUS_RESET_LOG_LIMIT))
			DevCon.WriteLn("FW HLE: bus reset/self-ID local_phy=%u local_node=0x%04x remote_phy=%u", LOCAL_PHY_ID, LOCAL_NODE_ID, REMOTE_PHY_ID);
	}

	void MaybeRaisePendingInterrupts()
	{
		if ((fwRu32(0x8420) & fwRu32(0x8424)) || (fwRu32(0x8428) & fwRu32(0x842c)) || (fwRu32(0x8430) & fwRu32(0x8434)))
			fwIrq();
	}

	void QueueWriteResponse(u32 request_header, u32 request_offset_high)
	{
		const u32 tlabel = (request_header >> 10) & 0x3f;
		const u32 dest_node = request_offset_high >> 16;
		const u32 bus_id = dest_node >> 6;
		DevCon.WriteLn("FW HLE: queue UBUF WRITE_RESPONSE req_hdr=0x%x tlabel=0x%x dest_node=0x%x bus=0x%x", request_header, tlabel, dest_node, bus_id);

		QueueUbufRx((bus_id << 22) | (KONAMI_RESPONSE_SPEED << 16) | (tlabel << 10) | (1u << 8) | (IEEE1394_TCODE_WRITE_RESPONSE << 4));
		QueueUbufRx((dest_node << 16));
		QueueUbufRx(0);
		QueueUbufRx(1);
		RaiseIntr0(FW_INTR0_URx);
	}

	void AckTransmit(u32 ack)
	{
		fwRu32(0x843c) = ack << 28;
		DevCon.WriteLn("FW HLE: tx ack ack=0x%x ack_status=0x%x", ack, fwRu32(0x843c));
		RaiseIntr0(FW_INTR0_AckRcvd);
	}

	void QueueReadResponse(u32 request_header, u32 request_offset_high, u32 value)
	{
		const u32 tlabel = (request_header >> 10) & 0x3f;
		const u32 dest_node = request_offset_high >> 16;
		const u32 bus_id = dest_node >> 6;
		DevCon.WriteLn("FW HLE: queue UBUF READQ_RESPONSE req_hdr=0x%x tlabel=0x%x dest_node=0x%x bus=0x%x value=0x%x", request_header, tlabel, dest_node, bus_id, value);

		QueueUbufRx((bus_id << 22) | (KONAMI_RESPONSE_SPEED << 16) | (tlabel << 10) | (1u << 8) | (IEEE1394_TCODE_READQ_RESPONSE << 4));
		QueueUbufRx((dest_node << 16));
		QueueUbufRx(0);
		QueueUbufRx(value);
		QueueUbufRx(1);
		RaiseIntr0(FW_INTR0_URx);
	}

	bool TryReadKonamiConfigRom(u32 offset_high, u32 offset_low, u32* value)
	{
		if ((offset_high & 0xffff) != 0xffff || offset_low < KONAMI_CROM_BASE)
			return false;

		const u32 relative_offset = offset_low - KONAMI_CROM_BASE;
		if ((relative_offset & 3) != 0)
			return false;

		const u32 index = relative_offset >> 2;
		if (index >= sizeof(KONAMI_IO_BOARD_CROM) / sizeof(KONAMI_IO_BOARD_CROM[0]))
			return false;

		*value = KONAMI_IO_BOARD_CROM[index];
		return true;
	}

	bool TryReadKonamiQuadlet(u32 offset_high, u32 offset_low, u32* value)
	{
		if (TryReadKonamiConfigRom(offset_high, offset_low, value))
			return true;

		if ((offset_high & 0xffff) == KONAMI_BOOT_READY_OFFSET_HIGH && offset_low == KONAMI_BOOT_READY_OFFSET_LOW)
		{
			// Captured response to 0xfffd:0x05735734 immediately before the first CF command.
			*value = 0x01000000;
			return true;
		}

		if ((offset_high & 0xffff) == KONAMI_BOOT_READY_OFFSET_HIGH && offset_low == KONAMI_RUNTIME_READY_OFFSET_LOW)
		{
			// Captured runtime response to 0xfffd:0x05735730 before command traffic resumes.
			*value = 0;
			return true;
		}

		return false;
	}

	void QueuePendingDbufQuadWrite(u32 offset_high, u32 offset_low, u32 payload)
	{
		if (ShouldLogLimited(s_sector_read_log_count, FW_SECTOR_LOG_LIMIT))
			DevCon.WriteLn("FW HLE: queue DBUF WRITEQ off_hi=0x%x off_low=0x%x payload=0x%x", offset_high, offset_low, payload);
		FwTrace("queue WRITEQ off_hi=0x%x off_low=0x%x payload=0x%x", offset_high, offset_low, payload);
		s_pending_dbuf_r0_rx_dma.emplace_back();
		s_pending_dbuf_r0_rx_fifo.push_back((0x3ffu << 22) | (KONAMI_RESPONSE_SPEED << 16) | (1u << 10) | (IEEE1394_TCODE_WRITEQ << 4));
		s_pending_dbuf_r0_rx_fifo.push_back((LOCAL_NODE_ID << 16) | offset_high);
		s_pending_dbuf_r0_rx_fifo.push_back(offset_low);
		s_pending_dbuf_r0_rx_fifo.push_back(payload);
		s_pending_dbuf_r0_rx_fifo.push_back(2);
	}

	void QueuePendingDbufBlockWrite(u32 offset_high, u32 offset_low, const u32* payload, u32 payload_quads)
	{
		const u32 byte_count = payload_quads * sizeof(u32);
		if (ShouldLogLimited(s_runtime_log_count, FW_RUNTIME_LOG_LIMIT))
			DevCon.WriteLn("FW HLE: queue DBUF WRITEB off_hi=0x%x off_low=0x%x bytes=0x%x", offset_high, offset_low, byte_count);
		FwTrace("queue WRITEB off_hi=0x%x off_low=0x%x bytes=0x%x", offset_high, offset_low, byte_count);
		s_pending_dbuf_r0_rx_dma.emplace_back();
		if (offset_high == 0x1000 && byte_count != 0)
		{
			PendingDbufDmaWrite& dma = s_pending_dbuf_r0_rx_dma.back();
			dma.active = true;
			dma.dest = offset_low;
			dma.data.resize(byte_count);
			std::memcpy(dma.data.data(), payload, byte_count);
		}
		s_pending_dbuf_r0_rx_fifo.push_back((0x3ffu << 22) | (KONAMI_RESPONSE_SPEED << 16) | (IEEE1394_TCODE_WRITEB << 4));
		s_pending_dbuf_r0_rx_fifo.push_back((LOCAL_NODE_ID << 16) | offset_high);
		s_pending_dbuf_r0_rx_fifo.push_back(offset_low);
		s_pending_dbuf_r0_rx_fifo.push_back(byte_count << 16);
		for (u32 i = 0; i < payload_quads; i++)
			s_pending_dbuf_r0_rx_fifo.push_back(payload[i]);
		s_pending_dbuf_r0_rx_fifo.push_back(2);
	}

	void QueuePendingDbufByteWrite(u32 offset_high, u32 offset_low, const u8* payload, u32 byte_count)
	{
		if (ShouldLogLimited(s_runtime_log_count, FW_RUNTIME_LOG_LIMIT))
			DevCon.WriteLn("FW HLE: queue DBUF WRITEB off_hi=0x%x off_low=0x%x bytes=0x%x", offset_high, offset_low, byte_count);
		FwTrace("queue WRITEB bytes off_hi=0x%x off_low=0x%x bytes=0x%x", offset_high, offset_low, byte_count);

		s_pending_dbuf_r0_rx_dma.emplace_back();
		if (offset_high == 0x1000 && byte_count != 0)
		{
			PendingDbufDmaWrite& dma = s_pending_dbuf_r0_rx_dma.back();
			dma.active = true;
			dma.dest = offset_low;
			dma.data.assign(payload, payload + byte_count);
		}

		s_pending_dbuf_r0_rx_fifo.push_back((0x3ffu << 22) | (KONAMI_RESPONSE_SPEED << 16) | (IEEE1394_TCODE_WRITEB << 4));
		s_pending_dbuf_r0_rx_fifo.push_back((LOCAL_NODE_ID << 16) | offset_high);
		s_pending_dbuf_r0_rx_fifo.push_back(offset_low);
		s_pending_dbuf_r0_rx_fifo.push_back(byte_count << 16);
		for (u32 offset = 0; offset < byte_count; offset += sizeof(u32))
		{
			u32 value = 0;
			for (u32 i = 0; i < sizeof(value) && offset + i < byte_count; i++)
				value |= static_cast<u32>(payload[offset + i]) << (24 - (i * 8));
			s_pending_dbuf_r0_rx_fifo.push_back(value);
		}
		s_pending_dbuf_r0_rx_fifo.push_back(2);
	}

	u32 CalculateReadStatusChecksum(const std::vector<u8>& data)
	{
		u32 checksum = 0;
		for (size_t offset = 0; offset < data.size(); offset += 2)
		{
			u32 word = static_cast<u32>(data[offset]) << 8;
			if (offset + 1 < data.size())
				word |= data[offset + 1];
			checksum = (checksum + word) & 0x7fffffffu;
		}
		return checksum;
	}

	bool QueuePendingSectorAndStatusPackets(u32 dest, const std::vector<u8>& data)
	{
		s_pending_dbuf_r0_rx_fifo.clear();
		s_pending_dbuf_r0_rx_dma.clear();
		const size_t chunks = (data.size() + SECTOR_SIZE - 1) / SECTOR_SIZE;
		if (ShouldLogLimited(s_sector_read_log_count, FW_SECTOR_LOG_LIMIT))
			DevCon.WriteLn("FW HLE: queue DBUF WRITEB off_hi=0x1000 off_low=0x%x bytes=0x%zx chunks=0x%zx", dest, data.size(), chunks);
		FwTrace("sector response queue dest=0x%x bytes=0x%zx chunks=0x%zx pending_before=0x%zx", dest, data.size(), chunks, s_pending_dbuf_r0_rx_fifo.size());

		if (!iopMemSafeWriteBytes(dest, data.data(), static_cast<u32>(data.size())))
		{
			DevCon.WriteLn("FW HLE: failed FireWire DMA write of 0x%zx bytes to IOP 0x%x", data.size(), dest);
			return false;
		}

		DevCon.WriteLn("FW HLE: FireWire DMA WRITEB off_hi=0x1000 off_low=0x%x bytes=0x%zx", dest, data.size());
		FwTrace("sector direct dma dest=0x%x bytes=0x%zx", dest, data.size());

		for (size_t offset = 0; offset < data.size(); offset += SECTOR_SIZE)
		{
			s_pending_dbuf_r0_rx_dma.emplace_back();
			size_t chunk = data.size() - offset;
			if (chunk > SECTOR_SIZE)
				chunk = SECTOR_SIZE;

			s_pending_dbuf_r0_rx_fifo.push_back((0x3ffu << 22) | (KONAMI_RESPONSE_SPEED << 16) | (IEEE1394_TCODE_WRITEB << 4));
			s_pending_dbuf_r0_rx_fifo.push_back((LOCAL_NODE_ID << 16) | 0x1000u);
			s_pending_dbuf_r0_rx_fifo.push_back(dest + static_cast<u32>(offset));
			s_pending_dbuf_r0_rx_fifo.push_back(static_cast<u32>(chunk) << 16);

			for (size_t chunk_offset = 0; chunk_offset < chunk; chunk_offset += sizeof(u32))
			{
				u32 value = 0;
				for (size_t i = 0; i < sizeof(value) && chunk_offset + i < chunk; i++)
					value |= static_cast<u32>(data[offset + chunk_offset + i]) << (24 - (i * 8));
				s_pending_dbuf_r0_rx_fifo.push_back(value);
			}

			s_pending_dbuf_r0_rx_fifo.push_back(2);
		}

		QueuePendingDbufQuadWrite(0xfffe, KONAMI_CF_STATUS_OFFSET, ByteSwap32(CalculateReadStatusChecksum(data)));
		return true;
	}

	size_t GetPendingDbufPacketQuadCount()
	{
		if (s_pending_dbuf_r0_rx_fifo.empty())
			return 0;

		const u32 tcode = (s_pending_dbuf_r0_rx_fifo[0] >> 4) & 0xf;
		switch (tcode)
		{
			case IEEE1394_TCODE_WRITEQ:
				return 5;
			case IEEE1394_TCODE_WRITEB:
				if (s_pending_dbuf_r0_rx_fifo.size() < 4)
					return 0;
				return 4 + ((s_pending_dbuf_r0_rx_fifo[3] >> 16) + 3) / sizeof(u32) + 1;
			default:
				return 0;
		}
	}

	void FlushPendingDbufR0RxPacket()
	{
		if (s_pending_dbuf_r0_rx_fifo.empty() || !s_dbuf_r0_rx_fifo.empty())
			return;

		const size_t packet_quads = GetPendingDbufPacketQuadCount();
		if (packet_quads == 0 || packet_quads > s_pending_dbuf_r0_rx_fifo.size())
		{
			DevCon.WriteLn("FW HLE: malformed deferred DBUF packet pending_quads=0x%zx", s_pending_dbuf_r0_rx_fifo.size());
			s_pending_dbuf_r0_rx_fifo.clear();
			s_pending_dbuf_r0_rx_dma.clear();
			return;
		}

		if (!s_pending_dbuf_r0_rx_dma.empty() && s_pending_dbuf_r0_rx_dma.front().active)
		{
			const PendingDbufDmaWrite& dma = s_pending_dbuf_r0_rx_dma.front();
			const bool dma_written = iopMemSafeWriteBytes(dma.dest, dma.data.data(), static_cast<u32>(dma.data.size()));
			DevCon.WriteLn("FW HLE: FireWire DMA WRITEB off_hi=0x1000 off_low=0x%x bytes=0x%zx ok=%u", dma.dest, dma.data.size(), dma_written ? 1 : 0);
			FwTrace("deliver dma dest=0x%x bytes=0x%zx ok=%u", dma.dest, dma.data.size(), dma_written ? 1 : 0);
		}

		for (size_t i = 0; i < packet_quads; i++)
			QueueDbufR0Rx(s_pending_dbuf_r0_rx_fifo[i]);
		s_pending_dbuf_r0_rx_fifo.erase(s_pending_dbuf_r0_rx_fifo.begin(), s_pending_dbuf_r0_rx_fifo.begin() + packet_quads);
		if (!s_pending_dbuf_r0_rx_dma.empty())
			s_pending_dbuf_r0_rx_dma.erase(s_pending_dbuf_r0_rx_dma.begin());
		DevCon.WriteLn("FW HLE: queued deferred DBUF R0 rx packet quads=0x%zx level=0x%x pending_quads=0x%zx pending_dma=0x%zx", packet_quads, fwRu32(0x84c0), s_pending_dbuf_r0_rx_fifo.size(), s_pending_dbuf_r0_rx_dma.size());
		// DRFR is reserved for self-ID in Konami's stack; it drains DBUF before the RX task can parse async packets.
		RaiseIntr0(FW_INTR0_URx);
	}

	bool HleReadSectors(u32 sector, u32 count, u32 dest)
	{
		if (count == 0)
			return true;

		const u64 offset = static_cast<u64>(sector) * SECTOR_SIZE;
		const u64 bytes64 = static_cast<u64>(count) * SECTOR_SIZE;
		if (bytes64 > 16 * 1024 * 1024)
		{
			DevCon.WriteLn("FW HLE: refusing oversized sector read sector=0x%x count=0x%x dest=0x%x", sector, count, dest);
			return false;
		}

		std::vector<u8> data(static_cast<size_t>(bytes64));
		std::FILE* file = std::fopen(WE2K3_BLOB_PATH, "rb");
		if (!file)
		{
			DevCon.WriteLn("FW HLE: failed to open %s", WE2K3_BLOB_PATH);
			return false;
		}

		if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0)
		{
			DevCon.WriteLn("FW HLE: failed to seek %s to 0x%llx", WE2K3_BLOB_PATH, static_cast<unsigned long long>(offset));
			std::fclose(file);
			return false;
		}

		const size_t bytes = static_cast<size_t>(bytes64);
		const size_t read = std::fread(data.data(), 1, bytes, file);
		std::fclose(file);
		if (read != bytes)
		{
			DevCon.WriteLn("FW HLE: short read %s offset=0x%llx requested=0x%zx read=0x%zx", WE2K3_BLOB_PATH, static_cast<unsigned long long>(offset), bytes, read);
			return false;
		}

		return QueuePendingSectorAndStatusPackets(dest, data);
	}

	bool HleBbsramCommand(const u32* payload, u32 payload_quads)
	{
		if (payload_quads < 4)
			return false;

		const u32 subop = payload[0];
		const u32 offset = payload[1];
		const u32 byte_count = payload[2];
		const u32 dest = payload[3];
		DevCon.WriteLn("FW HLE: BBSRAM command subop=0x%x offset=0x%x bytes=0x%x dest=0x%x", subop, offset, byte_count, dest);
		FwTrace("bbsram subop=0x%x offset=0x%x bytes=0x%x dest=0x%x", subop, offset, byte_count, dest);
		if (offset > BBSRAM_SIZE || byte_count > BBSRAM_SIZE - offset)
		{
			DevCon.WriteLn("FW HLE: BBSRAM request out of range subop=0x%x offset=0x%x bytes=0x%x dest=0x%x", subop, offset, byte_count, dest);
			return false;
		}

		if (subop == 0)
		{
			if (byte_count != 0 && dest != 0)
			{
				TraceBytes("bbsram read data", offset, s_bbsram + offset, byte_count);
				QueuePendingDbufByteWrite(0x1000, dest, s_bbsram + offset, byte_count);
			}
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_BBSRAM_STATUS_OFFSET, 0);
			return true;
		}

		if (subop == 1)
		{
			if (byte_count != 0 && dest != 0)
			{
				if (!iopMemSafeReadBytes(dest, s_bbsram + offset, byte_count))
				{
					DevCon.WriteLn("FW HLE: failed BBSRAM write DMA read src=0x%x bytes=0x%x", dest, byte_count);
					return false;
				}
				TraceIopBuffer("bbsram write source", dest, byte_count);
				TraceBytes("bbsram write data", offset, s_bbsram + offset, byte_count);
				s_bbsram_dirty = true;
				SaveBbsramIfDirty();
			}
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_BBSRAM_STATUS_OFFSET, 0);
			return true;
		}

		DevCon.WriteLn("FW HLE: unhandled BBSRAM subop=0x%x offset=0x%x bytes=0x%x dest=0x%x", subop, offset, byte_count, dest);
		return false;
	}

	bool HleDallasCommand(const u32* payload, u32 payload_quads)
	{
		if (payload_quads < 1)
			return false;

		const u32 subop = payload[0];
		const u32 key = payload_quads > 1 ? payload[1] : 0;
		const u32 offset = payload_quads > 2 ? payload[2] : 0;
		const u32 byte_count = payload_quads > 3 ? payload[3] : 0;
		const u32 dest = payload_quads > 4 ? payload[4] : 0;
		DevCon.WriteLn("FW HLE: DALLAS command subop=0x%x key=0x%x offset=0x%x bytes=0x%x dest=0x%x", subop, key, offset, byte_count, dest);
		FwTrace("dallas subop=0x%x key=0x%x offset=0x%x bytes=0x%x dest=0x%x", subop, key, offset, byte_count, dest);

		QueuePendingDbufBlockWrite(0xfffe, KONAMI_DALLAS_STATUS_OFFSET, KONAMI_DALLAS_NO_KEY_RESPONSE,
			sizeof(KONAMI_DALLAS_NO_KEY_RESPONSE) / sizeof(KONAMI_DALLAS_NO_KEY_RESPONSE[0]));
		return true;
	}

	bool HleFsciCommand(const u32* payload, u32 payload_quads)
	{
		const u32 subop = payload[0];
		const u32 byte_count = payload_quads > 1 ? payload[1] : 0;
		const u32 dest = payload_quads > 2 ? payload[2] : 0;
		DevCon.WriteLn("FW HLE: FSCI command subop=0x%x bytes=0x%x dest=0x%x", subop, byte_count, dest);
		FwTrace("fsci subop=0x%x bytes=0x%x dest=0x%x", subop, byte_count, dest);

		if (subop == 2 && byte_count != 0 && dest != 0)
		{
			const u32 response_bytes = std::min<u32>(byte_count, sizeof(KONAMI_FSCI_SERIAL_STREAM) - 1);
			QueuePendingDbufByteWrite(0x1000, dest, KONAMI_FSCI_SERIAL_STREAM, response_bytes);
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_FSCI_STATUS_OFFSET, ByteSwap32(response_bytes));
			return true;
		}

		QueuePendingDbufQuadWrite(0xfffe, KONAMI_FSCI_STATUS_OFFSET, 0);
		return true;
	}

	bool HleUartCommand(const u32* payload, u32 payload_quads)
	{
		if (payload_quads < 1)
			return false;

		const u32 subop = payload[0];
		const u32 word1 = payload_quads > 1 ? payload[1] : 0;
		const u32 word2 = payload_quads > 2 ? payload[2] : 0;
		const u32 status = ByteSwap32(2);
		DevCon.WriteLn("FW HLE: UART command subop=0x%x w1=0x%x w2=0x%x status=0x%x", subop, word1, word2, status);
		FwTrace("uart subop=0x%x w1=0x%x w2=0x%x status=0x%x", subop, word1, word2, status);

		QueuePendingDbufQuadWrite(0xfffe, KONAMI_UART_STATUS_OFFSET, status);
		if (subop == 2)
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_UART_STATUS_OFFSET, ByteSwap32(1));
		return true;
	}

	bool HleBootromCommand(const u32* payload, u32 payload_quads)
	{
		if (payload_quads < 4)
			return false;

		const u32 subop = payload[0];
		const u32 offset = payload[1];
		const u32 byte_count = payload[2];
		const u32 dest = payload[3];
		DevCon.WriteLn("FW HLE: BOOTROM command subop=0x%x offset=0x%x bytes=0x%x dest=0x%x", subop, offset, byte_count, dest);
		FwTrace("bootrom subop=0x%x offset=0x%x bytes=0x%x dest=0x%x", subop, offset, byte_count, dest);

		if (offset > BOOTROM_SIZE || byte_count > BOOTROM_SIZE - offset)
		{
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_BOOTROM_STATUS_OFFSET, 0x41000000);
			return true;
		}

		if (subop == 0)
		{
			if (byte_count != 0 && dest != 0)
				QueuePendingDbufByteWrite(0x1000, dest, s_bootrom + offset, byte_count);
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_BOOTROM_STATUS_OFFSET, 0x41000000);
			return true;
		}

		if (subop == 1)
		{
			if (byte_count != 0 && dest != 0 && !iopMemSafeReadBytes(dest, s_bootrom + offset, byte_count))
				DevCon.WriteLn("FW HLE: failed BOOTROM write DMA read src=0x%x bytes=0x%x", dest, byte_count);
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_BOOTROM_STATUS_OFFSET, 0x41000000);
			return true;
		}

		QueuePendingDbufQuadWrite(0xfffe, KONAMI_BOOTROM_STATUS_OFFSET, 0x41000000);
		return true;
	}

	bool TryHleKonamiCommand(u32 offset_low, const u32* payload, u32 payload_quads)
	{
		const u32 command_offset = offset_low & 0xfff;
		DevCon.WriteLn("FW HLE: TryHleKonamiCommand offset_low=0x%x command_offset=0x%x payload_quads=0x%x", offset_low, command_offset, payload_quads);
		FwTrace("cmd offset_low=0x%x command_offset=0x%x quads=0x%x p0=0x%x p1=0x%x p2=0x%x p3=0x%x", offset_low, command_offset, payload_quads,
			payload_quads > 0 ? payload[0] : 0, payload_quads > 1 ? payload[1] : 0, payload_quads > 2 ? payload[2] : 0, payload_quads > 3 ? payload[3] : 0);
		if (command_offset != KONAMI_CF_COMMAND_OFFSET && command_offset != KONAMI_ATA_COMMAND_OFFSET && payload_quads >= 1)
		{
			if (command_offset == 0x100 && HleUartCommand(payload, payload_quads))
				return true;

			if (command_offset == 0x120 && HleDallasCommand(payload, payload_quads))
				return true;

			if (command_offset == 0x140 && HleBbsramCommand(payload, payload_quads))
				return true;

			if (command_offset == 0xe0 && payload_quads >= 8)
			{
				static constexpr u32 JAMMA_INIT_RESPONSE[] = {
					0xfe4b109c, 0x00000000, 0x00000000, 0x00000000,
					JAMMA_P1_JVS_PRESENT,
					0x81000100, 0x00000000, 0x01020101,
					JAMMA_STATUS_NEUTRAL,
				};
				static constexpr u32 JAMMA_ATTACHED_INPUT[] = {
					0x98062caa, 0x00000000, 0x00000000, 0x00000000,
					JAMMA_P1_JVS_PRESENT, 0x80000000, 0x00000000, 0x01020101, JAMMA_STATUS_NEUTRAL,
				};
				QueuePendingDbufBlockWrite(0x1000, payload[0], JAMMA_INIT_RESPONSE,
					sizeof(JAMMA_INIT_RESPONSE) / sizeof(JAMMA_INIT_RESPONSE[0]));
				for (u32 i = 0; i < 8; i++)
					QueuePendingDbufBlockWrite(0x1000, payload[0], JAMMA_ATTACHED_INPUT,
						sizeof(JAMMA_ATTACHED_INPUT) / sizeof(JAMMA_ATTACHED_INPUT[0]));
				return true;
			}

			if (command_offset == 0x150 && HleBootromCommand(payload, payload_quads))
				return true;

			if (command_offset == 0x160 && payload_quads >= 1)
			{
				if ((payload[0] == 2 || payload[0] == 3) && payload_quads >= 3 && payload[2] != 0)
					TraceIopBuffer("fsci buffer before", payload[2], payload[1]);
				return HleFsciCommand(payload, payload_quads);
			}
		}

		if ((command_offset != KONAMI_CF_COMMAND_OFFSET && command_offset != KONAMI_ATA_COMMAND_OFFSET) || payload_quads < 4)
			return false;

		const u32 subop = payload[0];
		const u32 sector = payload[1];
		const u32 count = payload[2];
		const u32 dest = payload[3];
		const u32 p4 = payload_quads > 4 ? payload[4] : 0;
		const u32 p5 = payload_quads > 5 ? payload[5] : 0;
		const u32 p6 = payload_quads > 6 ? payload[6] : 0;
		const u32 p7 = payload_quads > 7 ? payload[7] : 0;
		DevCon.WriteLn("FW HLE: Konami command off=0x%x subop=0x%x w1=0x%x w2=0x%x w3=0x%x w4=0x%x w5=0x%x w6=0x%x w7=0x%x",
			command_offset, subop, sector, count, dest, p4, p5, p6, p7);

		if (command_offset == KONAMI_CF_COMMAND_OFFSET && subop == 0 && HleReadSectors(sector, count, dest))
		{
			if (ShouldLogLimited(s_sector_read_log_count, FW_SECTOR_LOG_LIMIT))
				DevCon.WriteLn("FW HLE: WE2K3 CF read sector=0x%x count=0x%x bytes=0x%x dest=0x%x", sector, count, count * SECTOR_SIZE, dest);
			return true;
		}
		else if (ShouldLogLimited(s_discovery_log_count, FW_DISCOVERY_LOG_LIMIT))
		{
			DevCon.WriteLn("FW HLE: unhandled Konami command off=0x%x subop=0x%x sector=0x%x count=0x%x dest=0x%x", command_offset, subop, sector, count, dest);
		}
		FwTrace("unhandled cmd off=0x%x subop=0x%x sector=0x%x count=0x%x dest=0x%x", command_offset, subop, sector, count, dest);

		return false;
	}

	void LogRuntimePayloadPreview(const char* prefix, u32 offset_high, u32 offset_low, const u32* payload, u32 payload_quads, bool handled)
	{
		ShouldLogLimited(s_runtime_log_count, FW_RUNTIME_LOG_LIMIT);

		const u32 p0 = payload_quads > 0 ? payload[0] : 0;
		const u32 p1 = payload_quads > 1 ? payload[1] : 0;
		const u32 p2 = payload_quads > 2 ? payload[2] : 0;
		const u32 p3 = payload_quads > 3 ? payload[3] : 0;
		const u32 p4 = payload_quads > 4 ? payload[4] : 0;
		const u32 p5 = payload_quads > 5 ? payload[5] : 0;
		const u32 p6 = payload_quads > 6 ? payload[6] : 0;
		const u32 p7 = payload_quads > 7 ? payload[7] : 0;
		DevCon.WriteLn("FW HLE: %s off_hi=0x%x off_low=0x%x payload_quads=0x%x handled=%u payload=%08x %08x %08x %08x %08x %08x %08x %08x",
			prefix, offset_high & 0xffff, offset_low, payload_quads, handled ? 1 : 0, p0, p1, p2, p3, p4, p5, p6, p7);
		for (u32 i = 8; i < payload_quads; i += 8)
		{
			const u32 q0 = payload_quads > i + 0 ? payload[i + 0] : 0;
			const u32 q1 = payload_quads > i + 1 ? payload[i + 1] : 0;
			const u32 q2 = payload_quads > i + 2 ? payload[i + 2] : 0;
			const u32 q3 = payload_quads > i + 3 ? payload[i + 3] : 0;
			const u32 q4 = payload_quads > i + 4 ? payload[i + 4] : 0;
			const u32 q5 = payload_quads > i + 5 ? payload[i + 5] : 0;
			const u32 q6 = payload_quads > i + 6 ? payload[i + 6] : 0;
			const u32 q7 = payload_quads > i + 7 ? payload[i + 7] : 0;
			DevCon.WriteLn("FW HLE: %s payload[%u]=%08x %08x %08x %08x %08x %08x %08x %08x",
				prefix, i, q0, q1, q2, q3, q4, q5, q6, q7);
		}
	}

	void LogPhtRequest(int channel);

	void TryProcessPhtWrite(int channel)
	{
		if (!s_pht_write_pending[channel])
			return;

		const u32 expected_bytes = s_pht_tx_expected_bytes[channel];
		if (expected_bytes == 0 || s_pht_tx_fifo[channel].size() * sizeof(u32) < expected_bytes)
			return;

		const u32 base = channel == 0 ? 0x8480 : 0x8500;
		const u32 hdr0 = fwRu32(base + 0x08);
		const u32 hdr1 = fwRu32(base + 0x0c);
		const u32 payload_quads = (expected_bytes + 3) >> 2;
		std::vector<u32> payload(payload_quads);
		for (u32 i = 0; i < payload_quads; i++)
			payload[i] = s_pht_tx_fifo[channel][i];

		const bool handled = TryHleKonamiCommand(hdr1, payload.data(), payload_quads);
		if (ShouldLogLimited(s_pht_log_count, FW_DISCOVERY_LOG_LIMIT))
		{
			DevCon.WriteLn("FW HLE: PHT%d write complete node=0x%x off_hi=0x%x off_low=0x%x bytes=0x%x handled=%u",
				channel, hdr0 >> 16, hdr0 & 0xffff, hdr1, expected_bytes, handled ? 1 : 0);
		}

		s_pht_write_pending[channel] = false;
		s_pht_tx_expected_bytes[channel] = 0;
		s_pht_tx_fifo[channel].clear();
		fwRu32(base + 0x24) = 0;
		RaiseIntr0(FW_INTR0_PBCntR);
	}

	void BeginPhtRequest(int channel, u32 control)
	{
		const u32 base = channel == 0 ? 0x8480 : 0x8500;
		const bool is_write = (control & PHT_CTRL_ST_EWREQ) != 0;

		LogPhtRequest(channel);
		if (!is_write)
		{
			fwRu32(base + 0x24) = 0;
			RaiseIntr0(FW_INTR0_PBCntR);
			return;
		}

		s_pht_write_pending[channel] = true;
		s_pht_tx_expected_bytes[channel] = fwRu32(base + 0x10) & 0xffff;
		s_pht_tx_fifo[channel].clear();
		TryProcessPhtWrite(channel);
	}

	void ProcessUbufTransmitPacket()
	{
		if (s_ubuf_tx_fifo.empty())
			return;

		const u32 header = s_ubuf_tx_fifo[0];
		const u32 tcode = (header >> 4) & 0xf;
		DevCon.WriteLn("FW HLE: process UBUF tx quads=%zu hdr=0x%x tcode=0x%x(%s) tlabel=0x%x speed=0x%x",
			s_ubuf_tx_fifo.size(), header, tcode, TcodeName(tcode), (header >> 10) & 0x3f, (header >> 16) & 0x7);

		if ((tcode == IEEE1394_TCODE_WRITEQ || tcode == IEEE1394_TCODE_WRITEB) && s_ubuf_tx_fifo.size() >= 4)
		{
			const u32 offset_high = s_ubuf_tx_fifo[1];
			const u32 offset_low = s_ubuf_tx_fifo[2];
			const u32 payload_start = (tcode == IEEE1394_TCODE_WRITEQ) ? 3 : 4;
			const u32 payload_quads = (s_ubuf_tx_fifo.size() > payload_start) ? static_cast<u32>(s_ubuf_tx_fifo.size() - payload_start) : 0;
			const u32 block_count = (tcode == IEEE1394_TCODE_WRITEB && s_ubuf_tx_fifo.size() > 3) ? s_ubuf_tx_fifo[3] : 0;
			DevCon.WriteLn("FW HLE: UBUF tx write raw off_hi=0x%x off_low=0x%x payload_start=0x%x payload_quads=0x%x block_count=0x%x",
				offset_high, offset_low, payload_start, payload_quads, block_count);

			bool handled = false;
			if (payload_quads != 0)
			{
				std::vector<u32> payload(payload_quads);
				for (u32 i = 0; i < payload_quads; i++)
					payload[i] = ByteSwap32(s_ubuf_tx_fifo[payload_start + i]);

				handled = TryHleKonamiCommand(offset_low, payload.data(), payload_quads);
				LogRuntimePayloadPreview("UBUF write", offset_high, offset_low, payload.data(), payload_quads, handled);
				if (handled)
					RaiseIntr0(FW_INTR0_PBCntR);
			}

			AckTransmit(KONAMI_ACK_COMPLETE);
		}
		else if ((tcode == IEEE1394_TCODE_READQ || tcode == IEEE1394_TCODE_READB) && s_ubuf_tx_fifo.size() >= 3)
		{
			const u32 offset_high = s_ubuf_tx_fifo[1];
			const u32 offset_low = s_ubuf_tx_fifo[2];
			u32 value = 0;
			const bool handled = TryReadKonamiQuadlet(offset_high, offset_low, &value);
			ShouldLogLimited((offset_high & 0xffff) == KONAMI_BOOT_READY_OFFSET_HIGH ? s_runtime_log_count : s_crom_log_count,
				((offset_high & 0xffff) == KONAMI_BOOT_READY_OFFSET_HIGH) ? FW_RUNTIME_LOG_LIMIT : FW_CROM_LOG_LIMIT);
			DevCon.WriteLn("FW HLE: UBUF read hdr=0x%x tlabel=0x%x node=0x%x off_hi=0x%x off_low=0x%x value=0x%x handled=%u",
				header, (header >> 10) & 0x3f, offset_high >> 16, offset_high & 0xffff, offset_low, value, handled ? 1 : 0);
			AckTransmit(KONAMI_ACK_PEND);
			QueueReadResponse(header, offset_high, value);
		}

		RaiseIntr1(FW_INTR1_UTD);
		s_ubuf_tx_fifo.clear();
		FlushPendingDbufR0RxPacket();
	}

	void LogPhtRequest(int channel)
	{
		const u32 base = channel == 0 ? 0x8480 : 0x8500;
		const u32 hdr0 = fwRu32(base + 0x08);
		const u32 hdr1 = fwRu32(base + 0x0c);
		const u32 hdr2 = fwRu32(base + 0x10);
		if (ShouldLogLimited(s_pht_log_count, FW_DISCOVERY_LOG_LIMIT))
		{
			DevCon.WriteLn("FW HLE: PHT%d hdr node=0x%x off_hi=0x%x off_low=0x%x tlabel=0x%x speed=0x%x bytes=0x%x ctrl=0x%x",
				channel, hdr0 >> 16, hdr0 & 0xffff, hdr1, (hdr2 >> 19) & 0x3f, (hdr2 >> 16) & 0x7, hdr2 & 0xffff, fwRu32(base));
		}
	}
}


void logControl0Regs(u32 value)
{
	if (!FW_VERBOSE_LOGS)
		return;

	bool rcv_self_id = (value & 0x8000'0000) >> 0x1f;
	bool sidf = (value & 0x4000'0000) >> 0x1e;
	uint8_t de_lim = (value & 0x3000'0000) >> 0x1c;
	bool tx_en = (value & 0x0800'0000) >> 0x1b;
	bool rx_en = (value & 0x0400'0000) >> 0x1a;
	bool tx_res = (value & 0x0200'0000) >> 0x19;
	bool rx_rst = (value & 0x0100'0000) >> 0x18;
	bool bus_id_rst = (value & 0x0080'0000) >> 0x17;
	bool c_mstr = (value & 0x0040'0000) >> 0x16;
	bool cyc_tmr_en = (value & 0x0020'0000) >> 0x15;
	bool ext_cyc = (value & 0x0010'0000) >> 0x14;
	bool root = (value & 0x0008'0000) >> 0x13;
	bool brde = (value & 0x0004'0000) >> 0x12;
	bool s_tardy = (value & 0x0002'0000) >> 0x11;
	bool loose_tight_iso = (value & 0x0001'0000) >> 0x10;
	uint8_t ret_lim = (value & 0x0000'f000) >> 0xc;
	uint16_t pri_lim = (value & 0x0000'0fc0) >> 0x6;
	bool rsp_0 = (value & 0x0000'0020) >> 0x5;
	bool u_rcv_m = (value & 0x0000'0010) >> 0x4;
	uint8_t ctrl_0_res = (value & 0x0000'000f);

	DevCon.WriteLn("##: Dumping control 0 regs ############");
	DevCon.WriteLn("##: Receive Self ID: 0x%x", rcv_self_id);
	DevCon.WriteLn("##: Self ID Format: 0x%x", sidf);
	DevCon.WriteLn("##: Data Error Retry Limit: 0x%x", de_lim);
	DevCon.WriteLn("##: Transmitter Enable: 0x%x", tx_en);
	DevCon.WriteLn("##: Receiver Enable: 0x%x", rx_en);
	DevCon.WriteLn("##: Transmitter Reset: 0x%x", tx_res);
	DevCon.WriteLn("##: Receiver Reset: 0x%x", rx_rst);
	DevCon.WriteLn("##: Bus ID Reset: 0x%x", bus_id_rst);
	DevCon.WriteLn("##: Cycle Master: 0x%x", c_mstr);
	DevCon.WriteLn("##: Cycle Timer Enable: 0x%x", cyc_tmr_en);
	DevCon.WriteLn("##: External Cycle: 0x%x", ext_cyc);
	DevCon.WriteLn("##: Root: 0x%x", root);
	DevCon.WriteLn("##: Busy Received Data Errors: 0x%x", brde);
	DevCon.WriteLn("##: Send Tardy: 0x%x", s_tardy);
	DevCon.WriteLn("##: Loose ISO Cycles: 0x%x", loose_tight_iso);
	DevCon.WriteLn("##: Retry Limit: 0x%x", ret_lim);
	DevCon.WriteLn("##: Priority Request Limit: 0x%x", pri_lim);
	DevCon.WriteLn("##: Route SelfID Packets to PHT0: 0x%x", rsp_0);
	DevCon.WriteLn("##: UBuf Receive Multiple Packets: 0x%x", u_rcv_m);
	DevCon.WriteLn("##: Reserved: 0x%x", ctrl_0_res);
	DevCon.WriteLn("#######################################");
}

void logPhyRegs()
{
	if (!FW_VERBOSE_LOGS)
		return;

	u32 value = PHYACC;
	uint8_t addr = (value & 0x0f00'0000u) >> 24;

	uint8_t physical_id;
	bool r;
	bool ps;

	bool rhb;
	bool ibr;
	uint8_t gap_count;

	uint8_t extended;
	uint8_t total_ports;

	uint8_t max_speed;
	uint8_t delay;

	bool l_ctrl;
	bool contender;
	uint8_t jitter;
	uint8_t pwr_class;

	bool watchdog;
	bool isbr;
	bool loop;
	bool pwr_fail;
	bool timeout;
	bool port_event;
	bool enab_accel;
	bool enab_multi;

	// 0x06 reg reserved

	uint8_t page_select;
	uint8_t port_select;

	// 0x08 page select properties
	// page_select = 0x0 Port Status
	uint8_t a_stat;
	uint8_t b_stat;
	bool child;
	bool connected;
	bool bias;
	bool disabled;

	uint8_t negotiated_speed;
	bool int_enable;
	bool fault;

	// page_select = 0x1 Vendor ID page
	uint8_t compliance_level;

	// 0x09 reserved

	uint8_t vendor_id_h;

	uint8_t vendor_id_m;

	uint8_t vendor_id_l;

	uint8_t product_id_h;

	uint8_t product_id_m;

	uint8_t product_id_l;

	// page_select = 0x7 Vendor Dependent page
	uint8_t pzadj;
	bool ext_dp_s100;
	bool enable_sr;

	uint8_t phy_reg = ReadPhyRegister(addr);

	switch (addr)
	{
		case 0x00:
			physical_id = (phy_reg >> 2) & 0x1f;
			r = (phy_reg >> 1) & 0x1;
			ps = phy_reg & 0x1;
			DevCon.WriteLn("##: Physical ID: 0x%x", physical_id);
			DevCon.WriteLn("##: Root: 0x%x", r);
			DevCon.WriteLn("##: Power Status: 0x%x", ps);
			break;
		case 0x01:
			rhb = (phy_reg >> 7) & 0x1;
			ibr = (phy_reg >> 6) & 0x1;
			gap_count = phy_reg & 0x3f;
			DevCon.WriteLn("##: Root Hold-Off Bit: 0x%x", rhb);
			DevCon.WriteLn("##: Initiate Bus Reset 0x%x", ibr);
			DevCon.WriteLn("##: Gap Count: 0x%x", gap_count);
			break;
		case 0x02:
			extended = (phy_reg >> 5) & 0x7;
			total_ports = phy_reg & 0x1f;
			DevCon.WriteLn("##: Extended 0x%x", extended);
			DevCon.WriteLn("##: Total Ports: 0x%x", total_ports);
			break;
		case 0x03:
			max_speed = (phy_reg >> 5) & 0x7;
			delay = phy_reg & 0xf;
			DevCon.WriteLn("##: Max Speed: 0x%x", max_speed);
			DevCon.WriteLn("##: Delay: 0x%x", delay);
			break;
		case 0x04:
			l_ctrl = (phy_reg >> 7) & 0x1;
			contender = (phy_reg >> 6) & 0x1;
			jitter = (phy_reg >> 3) & 0x7;
			pwr_class = phy_reg & 0x7;
			DevCon.WriteLn("##: Link Active Report Control: 0x%x", l_ctrl);
			DevCon.WriteLn("##: Contender: 0x%x", contender);
			DevCon.WriteLn("##: Jitter: 0x%x", jitter);
			DevCon.WriteLn("##: Power Class: 0x%x", pwr_class);
			break;
		case 0x05:
			watchdog = (phy_reg >> 7) & 0x1;
			isbr = (phy_reg >> 6) & 0x1;
			loop = (phy_reg >> 5) & 0x1;
			pwr_fail = (phy_reg >> 4) & 0x1;
			timeout = (phy_reg >> 3) & 0x1;
			port_event = (phy_reg >> 2) & 0x1;
			enab_accel = (phy_reg >> 1) & 0x1;
			enab_multi = phy_reg & 0x1;
			DevCon.WriteLn("##: Watchdog: 0x%x", watchdog);
			DevCon.WriteLn("##: Initiate Short Bus Reset: 0x%x", isbr);
			DevCon.WriteLn("##: Loop Detect: 0x%x", loop);
			DevCon.WriteLn("##: Cable Power Failure Detect: 0x%x", pwr_fail);
			DevCon.WriteLn("##: Timeout Detect: 0x%x", timeout);
			DevCon.WriteLn("##: Port Event Detect: 0x%x", port_event);
			DevCon.WriteLn("##: Enable Arbitration Acceleration: 0x%x", enab_accel);
			DevCon.WriteLn("##: Enable Multispeed Packet Concatenation: 0x%x", enab_multi);
			break;
		case 0x06:
			DevCon.WriteLn("##: Wrote to reserved reg");
			break;
		case 0x07:
			page_select = (phy_reg >> 5) & 0x7;
			port_select = phy_reg & 0xf;
			DevCon.WriteLn("##: Page Select: 0x%x", page_select);
			DevCon.WriteLn("##: Port Select: 0x%x", port_select);
			break;
		case 0x08:
			page_select = (phyregs[0x07] >> 5) & 0x7;
			port_select = phyregs[0x07] & 0xf;
			DevCon.WriteLn("##: Page Select: 0x%x", page_select);
			DevCon.WriteLn("##: Port Select: 0x%x", port_select);
			if (page_select == 0)
			{
				a_stat = (phy_reg >> 6) & 0x3;
				b_stat = (phy_reg >> 4) & 0x3;
				child = (phy_reg >> 3) & 0x1;
				connected = (phy_reg >> 2) & 0x1;
				bias = (phy_reg >> 1) & 0x1;
				disabled = (phy_reg >> 0) & 0x1;
				DevCon.WriteLn("##: TPA Line State: 0x%x", a_stat);
				DevCon.WriteLn("##: TPB Line State: 0x%x", b_stat);
				DevCon.WriteLn("##: Child: 0x%x", child);
				DevCon.WriteLn("##: Connected: 0x%x", connected);
				DevCon.WriteLn("##: Bias Voltage: 0x%x", bias);
				DevCon.WriteLn("##: Port Disabled: 0x%x", disabled);
			}
			if (page_select == 1)
			{
				compliance_level = phy_reg;
				DevCon.WriteLn("##: Compliance Level: 0x%x", compliance_level);
			}
			if (page_select == 7)
			{
				pzadj = (phy_reg >> 4) & 0xf;
				ext_dp_s100 = (phy_reg >> 1) & 0x1;
				enable_sr = phy_reg & 0x1;
				DevCon.WriteLn("##: PLL Adjust: 0x%x", pzadj);
				DevCon.WriteLn("##: Extend Data Prefix for S100 Packets: 0x%x", ext_dp_s100);
				DevCon.WriteLn("##: Enable Suspend Resume: 0x%x", enable_sr);
			}
			break;
		case 0x09:
			page_select = (phyregs[0x07] >> 5) & 0x7;
			port_select = phyregs[0x07] & 0xf;
			DevCon.WriteLn("##: Page Select: 0x%x", page_select);
			DevCon.WriteLn("##: Port Select: 0x%x", port_select);
			if (page_select == 0)
			{
				negotiated_speed = (phy_reg >> 5) & 0x7;
				int_enable = (phy_reg >> 4) & 0x1;
				fault = (phy_reg >> 3) & 0x1;
				DevCon.WriteLn("##: Maximum Speed Negotiated: 0x%x", negotiated_speed);
				DevCon.WriteLn("##: Enable Port Event Interrupts: 0x%x", int_enable);
				DevCon.WriteLn("##: Fault: 0x%x", fault);
			}
			if (page_select == 1)
			{
				DevCon.WriteLn("## Reserved");
			}
			if (page_select == 7)
			{
				DevCon.WriteLn("## Reserved");
			}
			break;
		case 0x0a:
			page_select = (phyregs[0x07] >> 5) & 0x7;
			port_select = phyregs[0x07] & 0xf;
			DevCon.WriteLn("##: Page Select: 0x%x", page_select);
			DevCon.WriteLn("##: Port Select: 0x%x", port_select);
			if (page_select == 0)
			{
				DevCon.WriteLn("## Reserved");
			}
			if (page_select == 1)
			{
				vendor_id_h = phy_reg;
				DevCon.WriteLn("##: Vendor ID: 0x%x", vendor_id_h);
			}
			if (page_select == 7)
			{
				DevCon.WriteLn("## Reserved");
			}
			break;
		case 0x0b:
			page_select = (phyregs[0x07] >> 5) & 0x7;
			port_select = phyregs[0x07] & 0xf;
			DevCon.WriteLn("##: Page Select: 0x%x", page_select);
			DevCon.WriteLn("##: Port Select: 0x%x", port_select);
			if (page_select == 0)
			{
				DevCon.WriteLn("## Reserved");
			}
			if (page_select == 1)
			{
				vendor_id_m = phy_reg;
				DevCon.WriteLn("##: Vendor ID: 0x%x", vendor_id_m);
			}
			if (page_select == 7)
			{
				DevCon.WriteLn("## Reserved");
			}
			break;
		case 0x0c:
			page_select = (phyregs[0x07] >> 5) & 0x7;
			port_select = phyregs[0x07] & 0xf;
			DevCon.WriteLn("##: Page Select: 0x%x", page_select);
			DevCon.WriteLn("##: Port Select: 0x%x", port_select);
			if (page_select == 0)
			{
				DevCon.WriteLn("## Reserved");
			}
			if (page_select == 1)
			{
				vendor_id_l = phy_reg;
				DevCon.WriteLn("##: Vendor ID: 0x%x", vendor_id_l);
			}
			if (page_select == 7)
			{
				DevCon.WriteLn("## Reserved");
			}
			break;
		case 0x0d:
			page_select = (phyregs[0x07] >> 5) & 0x7;
			port_select = phyregs[0x07] & 0xf;
			DevCon.WriteLn("##: Page Select: 0x%x", page_select);
			DevCon.WriteLn("##: Port Select: 0x%x", port_select);
			if (page_select == 0)
			{
				DevCon.WriteLn("## Reserved");
			}
			if (page_select == 1)
			{
				product_id_h = phy_reg;
				DevCon.WriteLn("##: Product ID: 0x%x", product_id_h);
			}
			if (page_select == 7)
			{
				DevCon.WriteLn("## Reserved");
			}
			break;
		case 0x0e:
			page_select = (phyregs[0x07] >> 5) & 0x7;
			port_select = phyregs[0x07] & 0xf;
			DevCon.WriteLn("##: Page Select: 0x%x", page_select);
			DevCon.WriteLn("##: Port Select: 0x%x", port_select);
			if (page_select == 0)
			{
				DevCon.WriteLn("## Reserved");
			}
			if (page_select == 1)
			{
				product_id_m = phy_reg;
				DevCon.WriteLn("##: Product ID: 0x%x", product_id_m);
			}
			if (page_select == 7)
			{
				DevCon.WriteLn("## Reserved");
			}
			break;
		case 0x0f:
			page_select = (phyregs[0x07] >> 5) & 0x7;
			port_select = phyregs[0x07] & 0xf;
			DevCon.WriteLn("##: Page Select: 0x%x", page_select);
			DevCon.WriteLn("##: Port Select: 0x%x", port_select);
			if (page_select == 0)
			{
				DevCon.WriteLn("## Reserved");
			}
			if (page_select == 1)
			{
				product_id_l = phy_reg;
				DevCon.WriteLn("##: Product ID: 0x%x", product_id_l);
			}
			if (page_select == 7)
			{
				DevCon.WriteLn("## Reserved");
			}
			break;

	}
}

void logPhyAccessRegs()
{
	if (!FW_VERBOSE_LOGS)
		return;

	u32 value = PHYACC;
	bool rd_phy = (value & 0x8000'0000u) > 0;
	bool wr_phy = (value & 0x4000'0000u) > 0;
	uint8_t phy_reg_addr = (value & 0x0f00'0000u) >> 24;
	uint8_t phy_rg_dat = (value & 0x00ff'0000u) >> 16;
	uint8_t phy_rx_adr = (value & 0x0000'0f00u) >> 8;
	uint8_t phy_rx_dat = (value & 0x0000'00ffu);

	DevCon.WriteLn("##: Dumping PHY regs ############");
	DevCon.WriteLn("##: Read PHY: 0x%x", rd_phy);
	DevCon.WriteLn("##: Write PHY: 0x%x", wr_phy);
	DevCon.WriteLn("##: Phy Reg Addr: 0x%x", phy_reg_addr);
	DevCon.WriteLn("##: Phy Reg Data: 0x%x", phy_rg_dat);
	DevCon.WriteLn("##: Phy Receive Addr: 0x%x", phy_rx_adr);
	DevCon.WriteLn("##: Phy Receive Data: 0x%x", phy_rx_dat);
	DevCon.WriteLn("#################################");
}

void logFwAction(u32 addr, u32 value, bool write)
{
	if (!FW_VERBOSE_LOGS)
		return;

	const char* mode;
	const char* writeS = "write";
	const char* readS = "read";
	if (write != 0)
	{
		mode = writeS;
	}
	else
	{
		mode = readS;
	}

	switch (addr)
	{
		case 0x1f808400:
			DevCon.WriteLn("FW: %s node id 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808404:
			DevCon.WriteLn("FW: %s cycle time 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808408:
			DevCon.WriteLn("FW: %s ctrl 0 0x%x: 0x%x", mode, addr, value);
			logControl0Regs(value);
			break;
		case 0x1f80840c:
			DevCon.WriteLn("FW: %s ctrl 1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808410:
			DevCon.WriteLn("FW: %s ctrl 2 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808414:
			DevCon.WriteLn("FW: %s PHY Access 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808418:
		case 0x1f80841c:
			DevCon.WriteLn("FW: %s Unknown Register 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808420:
			DevCon.WriteLn("FW: %s interrupt 0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808424:
			DevCon.WriteLn("FW: %s interrupt mask 0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808428:
			DevCon.WriteLn("FW: %s interrupt 1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f80842c:
			DevCon.WriteLn("FW: %s interrupt mask 1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808430:
			DevCon.WriteLn("FW: %s interrupt 2 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808434:
			DevCon.WriteLn("FW: %s interrupt mask 2 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808438:
			DevCon.WriteLn("FW: %s dmar 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f80843c:
			DevCon.WriteLn("FW: %s ack status 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808440:
			DevCon.WriteLn("FW: %s ubuf transmit next 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808444:
			DevCon.WriteLn("FW: %s ubuf transmit last 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808448:
			DevCon.WriteLn("FW: %s ubuf transmit clear 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f80844c:
			DevCon.WriteLn("FW: %s ubuf receive clear 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808450:
			DevCon.WriteLn("FW: %s ubuf receive 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808454:
			DevCon.WriteLn("FW: %s ubuf receive level 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808458:
		case 0x1f80845c:
		case 0x1f808460:
		case 0x1f808464:
		case 0x1f808468:
		case 0x1f80846c:
			DevCon.WriteLn("FW: %s unmapped 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808470:
		case 0x1f808474:
		case 0x1f808478:
			DevCon.WriteLn("FW: %s unknown register 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f80847c:
			DevCon.WriteLn("FW: %s power management register 0x%x: 0x%x", mode, addr, value);
			// iLinkman sets this to 0x40 then 0x0 after delay to shutdown the link and phy
			// Konami loader sets this to 0x41 on boot, likely to do the same?
			// Then sets value to 0x1
			break;
		case 0x1f808480:
			DevCon.WriteLn("FW: %s PHT ctrl st r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808484:
			DevCon.WriteLn("FW: %s PHT split to r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808488:
			DevCon.WriteLn("FW: %s PHT req res hdr0 r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f80848c:
			DevCon.WriteLn("FW: %s PHT req res hdr1 r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808490:
			DevCon.WriteLn("FW: %s PHT req res hdr2 r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808494:
			DevCon.WriteLn("FW: %s STR x NID Sel0 r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808498:
			DevCon.WriteLn("FW: %s STR x NID Sel1 r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f80849c:
			DevCon.WriteLn("FW: %s STR x HDR r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f8084a0:
			DevCon.WriteLn("FW: %s STT x HDR r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f8084a4:
			DevCon.WriteLn("FW: %s DTrans CTRL 0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f8084a8:
			DevCon.WriteLn("FW: %s CIP Hdr Tx 0 r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f8084ac:
			DevCon.WriteLn("FW: %s CIP Hdr Tx 1 r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f8084b0:
			DevCon.WriteLn("FW: %s padding 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f8084b4:
			DevCon.WriteLn("FW: %s STT x time stamp offset r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f8084b8:
			DevCon.WriteLn("FW: %s dma ctrl SR0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f8084bc:
			DevCon.WriteLn("FW: %s dma trans TRSH0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f8084c0:
			if (write)
				DevCon.WriteLn("FW: %s dbuf FIFO lvl r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f8084c4:
			DevCon.WriteLn("FW: %s dbuf Tx data r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f8084c8:
			if (write)
				DevCon.WriteLn("FW: %s dbuf Rx data r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f8084cc:
			DevCon.WriteLn("FW: %s dbuf watermarks r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f8084d0:
			DevCon.WriteLn("FW: %s dbuf FIFO size r0 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f8084d4:
		case 0x1f8084d8:
		case 0x1f8084dc:
		case 0x1f8084e0:
		case 0x1f8084e4:
		case 0x1f8084e8:
		case 0x1f8084ec:
		case 0x1f8084f0:
		case 0x1f8084f4:
		case 0x1f8084f8:
		case 0x1f8084fc:
			DevCon.WriteLn("FW: %s unmapped 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808500:
			DevCon.WriteLn("FW: %s PHT ctrl st r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808504:
			DevCon.WriteLn("FW: %s PHT split to r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808508:
			DevCon.WriteLn("FW: %s PHT req res hdr0 r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f80850c:
			DevCon.WriteLn("FW: %s PHT req res hdr1 r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808510:
			DevCon.WriteLn("FW: %s PHT req res hdr2 r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808514:
			DevCon.WriteLn("FW: %s STR x NID Sel0 r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808518:
			DevCon.WriteLn("FW: %s STR x NID Sel1 r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f80851c:
			DevCon.WriteLn("FW: %s STR x HDR r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808520:
			DevCon.WriteLn("FW: %s STT x HDR r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808524:
			DevCon.WriteLn("FW: %s DTrans CTRL 1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808528:
			DevCon.WriteLn("FW: %s CIP Hdr Tx 0 r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f80852c:
			DevCon.WriteLn("FW: %s CIP Hdr Tx 1 r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808530:
			DevCon.WriteLn("FW: %s padding 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808534:
			DevCon.WriteLn("FW: %s STT x time stamp offset r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808538:
			DevCon.WriteLn("FW: %s dma ctrl SR1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f80853c:
			DevCon.WriteLn("FW: %s dma trans TRSH1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808540:
			DevCon.WriteLn("FW: %s dbuf FIFO lvl r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808544:
			DevCon.WriteLn("FW: %s dbuf Tx data r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808548:
			DevCon.WriteLn("FW: %s dbuf Rx data r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f80854c:
			DevCon.WriteLn("FW: %s dbuf watermarks r1 0x%x: 0x%x", mode, addr, value);
			break;
		case 0x1f808550:
			DevCon.WriteLn("FW: %s dbuf FIFO size r1 0x%x: 0x%x", mode, addr, value);
			break;
	}
}

	s32 FWopen()
	{
		InitializePhyRegisters();
		s_ubuf_rx_fifo.clear();
		s_ubuf_tx_fifo.clear();
		s_pending_dbuf_r0_rx_fifo.clear();
		s_pending_dbuf_r0_rx_dma.clear();
		s_dbuf_r0_rx_fifo.clear();
		std::memset(s_bootrom, 0xff, sizeof(s_bootrom));
		std::memcpy(s_bootrom, KONAMI_FACTORY_MAC, sizeof(KONAMI_FACTORY_MAC));
		std::memcpy(s_bootrom + 0xf000, KONAMI_MAC_BACKUP, sizeof(KONAMI_MAC_BACKUP));
		LoadBbsram();
		for (int channel = 0; channel < 2; channel++)
		{
			s_pht_tx_fifo[channel].clear();
			s_pht_tx_expected_bytes[channel] = 0;
			s_pht_write_pending[channel] = false;
		}
		s_bus_reset_log_count = 0;
		s_discovery_log_count = 0;
		s_crom_log_count = 0;
		s_dbuf_log_count = 0;
		s_ubuf_log_count = 0;
		s_intr_log_count = 0;
		s_sector_read_log_count = 0;
		s_pht_log_count = 0;
		s_runtime_log_count = 0;
		FwTrace("FWopen");
	// Initializing our registers.
	fwregs = (s8*)calloc(0x10000, 1);
	if (fwregs == NULL)
	{
		DevCon.WriteLn("FW: Error allocating Memory");
		return -1;
	}
	fwRu32(0x8400) = LOCAL_NODE_ID_REGISTER;
	fwRu32(0x8410) = 0x8;
	UpdateUbufRxLevel();
	UpdateDbufR0RxLevel();
	return 0;
}

void FWclose()
{
	SaveBbsramIfDirty();
	FwTrace("FWclose");
	// Freeing the registers.
	free(fwregs);
	fwregs = NULL;
}

void PHYWrite()
{
	u8 reg = (PHYACC >> 24) & 0xf;
	u8 data = (PHYACC >> 16) & 0xff;

	phyregs[reg] = data;
	if (reg == 0x01 && (data & PHY_REG01_IBR))
	{
		phyregs[reg] &= ~PHY_REG01_IBR;
		TriggerBusReset();
	}
	else if (reg == 0x05 && (data & PHY_REG05_ISBR))
	{
		phyregs[reg] = (phyregs[reg] & ~PHY_REG05_ISBR) | PHY_REG05_EN_ACCL | PHY_REG05_EN_MULTI;
		TriggerBusReset();
	}

	bool readback = (PHYACC & 0x8000'0000u) > 0;

	PHYACC &= 0xBFFF'FFFF; // Clear WrPhy bit

	if (readback)
	{
		PHYRead();
	}
}

void PHYRead()
{
	u8 reg = (PHYACC >> 24) & 0xf;

	PHYACC &= 0x7FFF'F000; // Clear RdPhy bit and RX data

	PHYACC |= ReadPhyRegister(reg) | (reg << 8);

	RaiseIntr0(FW_INTR0_PhyRRx);
}

u32 FWread32(u32 addr)
{
	u32 ret = 0;

	switch (addr)
	{
		//Node ID Register the top part is default, bottom part i got from my ps2
		case 0x1f808400:
			ret = fwRu32(addr);
			break;
		// Control Register 2
		case 0x1f808410:
			ret = fwRu32(addr); //SCLK OK (Needs to be set when FW is "Ready"
			break;
		//Interrupt 0 Register
		case 0x1f808420:
			ret = fwRu32(addr);
			break;
		case 0x1f808450:
			ret = PopUbufRx();
			break;
		case 0x1f808454:
			ret = static_cast<u32>(s_ubuf_rx_fifo.size());
			fwRu32(addr) = ret;
			break;
		case 0x1f8084c0:
			ret = static_cast<u32>((s_dbuf_r0_rx_fifo.size() * sizeof(u32)) << 16);
			fwRu32(addr) = ret;
			break;
		case 0x1f8084c8:
			ret = PopDbufR0Rx();
			break;

		//Dunno what this is, but my home console always returns this value 0x10000001
		//Seems to be related to the Node ID however (does some sort of compare/check)
		case 0x1f80847c:
			ret = 0x10000001;
			break;

		default:
			// By default, read fwregs.
			ret = fwRu32(addr);
			break;
	}

	logFwAction(addr, ret, false);
	if (addr == 0x1f808414)
	{
		logPhyAccessRegs();
		logPhyRegs();
	}
	return ret;
}

void FWwrite32(u32 addr, u32 value)
{
	logFwAction(addr, value, true);
	switch (addr)
	{
		case 0x1f808400:
			fwRu32(addr) = LOCAL_NODE_ID_REGISTER;
			break;
		//		Include other memory locations we want to catch here.
		//		For example:
		//
		//		case 0x1f808400:
		//		case 0x1f808414:
		//		case 0x1f808420:
		//		case 0x1f808428:
		//		case 0x1f808430:
		//

		//PHY access
		case 0x1f808414:
			//If in read mode (top bit set) we read the PHY register requested then set the RRx interrupt if it's enabled
			//Im presuming we send that back to pcsx2 then. This register stores the result, plus whatever was written (minus the read/write flag
			fwRu32(addr) = value;   //R/W Bit cleaned in underneath function
			logPhyAccessRegs();
			if (value & 0x40000000) //Writing to PHY, write can also request a readback by setting read bit so this check is done first
			{
				PHYWrite();
			}
			else if (value & 0x80000000) //Reading from PHY
			{
				PHYRead();
			}
			logPhyRegs();
			break;

		//Control Register 0
		case 0x1f808408:
			//This enables different functions of the link interface
			//Just straight writes, should brobably struct these later.
			//Default written settings (on unreal tournament) are
			//Urcv M = 1
			//RSP 0 = 1
			//Retlim = 0xF
			//Cyc Tmr En = 1
			//Bus ID Rst = 1
			//Rcv Self ID = 1
			fwRu32(addr) = value;
			//	if((value & 0x800000) && (fwRu32(0x842C) & 0x2))
			//	{
			//		fwRu32(0x8428) |= 0x2;
			//		FWirq();
			//	}
			fwRu32(addr) &= ~FW_CTRL0_BusIDRst;
			fwRu32(addr) |= FW_CTRL0_Root;
			break;
		//Control Register 2
		case 0x1f808410: // fwRu32(addr) = value; break;
			//Ignore writes to this for now, apart from 0x2 which is Link Power Enable
			//0x8 is SCLK OK (Ready) which should be set for emulation
			fwRu32(addr) = 0x8 | value & 0x2;
			break;
		//Interrupt 0 Register
		case 0x1f808420:
		//Interrupt 1 Register
		case 0x1f808428:
		//Interrupt 2 Register
		case 0x1f808430:
			if (addr == 0x1f808420 && ShouldLogLimited(s_intr_log_count, FW_INTR_LOG_LIMIT))
				DevCon.WriteLn("FW HLE: intr0 ack value=0x%x before=0x%x mask=0x%x", value, fwRu32(addr), fwRu32(0x8424));
			//Writes to interrupt register clears the corresponding interrupt bit
			fwRu32(addr) &= ~value;
			break;
		//Interrupt 0 Register Mask
		case 0x1f808424:
		//Interrupt 1 Register Mask
		case 0x1f80842C:
		//Interrupt 2 Register Mask
		case 0x1f808434:
			//These are direct writes (as it's a mask!)
			if (addr == 0x1f808424 && value != 0)
				value |= FW_INTR0_PhyRst;
			fwRu32(addr) = value;
			MaybeRaisePendingInterrupts();
			break;
		case 0x1f808440:
			s_ubuf_tx_fifo.push_back(value);
			fwRu32(addr) = value;
			break;
		case 0x1f808444:
			s_ubuf_tx_fifo.push_back(value);
			fwRu32(addr) = value;
			ProcessUbufTransmitPacket();
			break;
		case 0x1f808448:
			s_ubuf_tx_fifo.clear();
			fwRu32(addr) = value;
			break;
		case 0x1f80844c:
			s_ubuf_rx_fifo.clear();
			UpdateUbufRxLevel();
			fwRu32(addr) = value;
			break;
		case 0x1f8084c0:
			if (value & DBUF_FIFO_RESET_RX)
				s_dbuf_r0_rx_fifo.clear();
			UpdateDbufR0RxLevel();
			fwRu32(addr) |= value & DBUF_FIFO_RESET_TX;
			break;
		case 0x1f808480:
		case 0x1f808500:
			fwRu32(addr) = value & ~PHT_CTRL_ST_PHTRst;
			if (value & PHT_CTRL_ST_EWREQ_ERREQ)
				BeginPhtRequest(addr == 0x1f808480 ? 0 : 1, value);
			break;
		case 0x1f8084c4:
			s_pht_tx_fifo[0].push_back(value);
			fwRu32(addr) = value;
			fwRu32(0x84c0) = static_cast<u32>(s_pht_tx_fifo[0].size() << 16);
			TryProcessPhtWrite(0);
			break;
		case 0x1f808544:
			s_pht_tx_fifo[1].push_back(value);
			fwRu32(addr) = value;
			fwRu32(0x8540) = static_cast<u32>(s_pht_tx_fifo[1].size() << 16);
			TryProcessPhtWrite(1);
			break;
		//DMA Control and Status Register 0
		case 0x1f8084B8:
			fwRu32(addr) = value;
			break;
		//DMA Control and Status Register 1
		case 0x1f808538:
			fwRu32(addr) = value;
			break;
		default:
			// By default, just write it to fwregs.
			fwRu32(addr) = value;
			break;
	}
}
