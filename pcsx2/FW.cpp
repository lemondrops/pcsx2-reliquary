// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "FW.h"

#include "IopDma.h"
#include "IopHw.h"
#include "IopMem.h"
#include "R3000A.h"

#include "FireWire/deviceproxy.h"

#include "common/Console.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

static u8 phyregs[16];
s8* fwregs;

namespace
{
	constexpr bool FW_VERBOSE_LOGS = false;
	constexpr u32 FW_BUS_RESET_LOG_LIMIT = 4;
	constexpr u32 FW_DISCOVERY_LOG_LIMIT = 16;
	constexpr u32 FW_UBUF_LOG_LIMIT = 32;
	constexpr u32 FW_INTR_LOG_LIMIT = 16;
	constexpr u32 FW_REMOTE_WRITE_LOG_LIMIT = 256;

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
	constexpr u32 REMOTE_RESPONSE_SPEED = 2;
	constexpr u32 IEEE1394_ACK_COMPLETE = 1;
	constexpr u32 IEEE1394_ACK_PEND = 2;
	constexpr u32 SELF_ID_PORT_NOT_CONNECTED = 1;
	constexpr u32 SELF_ID_PORT_PARENT = 2;
	constexpr u32 SELF_ID_PORT_CHILD = 3;
	constexpr u32 LOCAL_NODE_ID = 0xffc0 | LOCAL_PHY_ID;
	constexpr u32 LOCAL_NODE_ID_REGISTER = (0x3ffu << 22) | (LOCAL_PHY_ID << 16) | 1u;

	struct PendingDbufDmaWrite
	{
		bool active = false;
		u32 dest = 0;
		std::vector<u8> data;
	};

	std::vector<u32> s_ubuf_rx_fifo;
	std::vector<u32> s_ubuf_tx_fifo;
	std::vector<u32> s_pending_dbuf_r0_rx_fifo;
	std::vector<PendingDbufDmaWrite> s_pending_dbuf_r0_rx_dma;
	std::vector<u32> s_dbuf_r0_rx_fifo;
	std::vector<u32> s_pht_tx_fifo[2];
	u32 s_pht_tx_expected_bytes[2];
	bool s_pht_write_pending[2];
	std::unique_ptr<FireWire::FireWireDevice> s_active_device;
	const FireWire::FireWireDeviceProxy* s_active_device_proxy = nullptr;
	u32 s_bus_reset_log_count;
	u32 s_discovery_log_count;
	u32 s_dbuf_log_count;
	u32 s_ubuf_log_count;
	u32 s_intr_log_count;
	u32 s_pht_log_count;
	u32 s_remote_write_log_count;

	bool ShouldLogLimited(u32& counter, u32 limit)
	{
		counter++;
		return counter <= limit;
	}

	u32 ByteSwap32(u32 value)
	{
		return (value >> 24) | ((value >> 8) & 0x0000ff00) | ((value & 0x0000ff00) << 8) | (value << 24);
	}

	void UpdateUbufRxLevel()
	{
		fwRu32(FW_UBUF_RX_LVL) = static_cast<u32>(s_ubuf_rx_fifo.size());
	}

	void UpdateDbufR0RxLevel()
	{
		fwRu32(FW_DBUF_FIFO_LV0) = static_cast<u32>((s_dbuf_r0_rx_fifo.size() * sizeof(u32)) << 16);
	}

	void RaiseIntr0(u32 bits)
	{
		fwRu32(FW_INTR0) |= bits;
		if (ShouldLogLimited(s_intr_log_count, FW_INTR_LOG_LIMIT))
			DevCon.WriteLn("FW HLE: intr0 raise bits=0x%x intr0=0x%x mask=0x%x", bits, fwRu32(FW_INTR0), fwRu32(FW_INTR0_MASK));
		if (fwRu32(FW_INTR0_MASK) & bits)
			fwIrq();
	}

	void RaiseIntr1(u32 bits)
	{
		fwRu32(FW_INTR1) |= bits;
		if (fwRu32(FW_INTR1_MASK) & bits)
			fwIrq();
	}

	void QueueUbufRx(u32 value)
	{
		s_ubuf_rx_fifo.push_back(value);
		UpdateUbufRxLevel();
		if (FW_VERBOSE_LOGS)
			DevCon.WriteLn("FW HLE: UBUF rx queue value=0x%x level=%zu", value, s_ubuf_rx_fifo.size());
	}

	u32 PopUbufRx()
	{
		if (s_ubuf_rx_fifo.empty())
			return 0;

		u32 value = s_ubuf_rx_fifo.front();
		s_ubuf_rx_fifo.erase(s_ubuf_rx_fifo.begin());
		UpdateUbufRxLevel();
		if (FW_VERBOSE_LOGS && ShouldLogLimited(s_ubuf_log_count, FW_UBUF_LOG_LIMIT))
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
		if (FW_VERBOSE_LOGS && became_empty && ShouldLogLimited(s_dbuf_log_count, FW_REMOTE_WRITE_LOG_LIMIT))
			DevCon.WriteLn("FW HLE: DBUF R0 rx pop value=0x%x remaining=%zu", value, s_dbuf_r0_rx_fifo.size());
		if (became_empty)
			FlushPendingDbufR0RxPacket();
		return value;
	}

	u8 ReadPhyRegister(u8 reg)
	{
		if (reg == 0x08 && ((phyregs[0x07] >> 5) & 0x7) == 0)
		{
			const u8 port = phyregs[0x07] & 0xf;
			return (port == 0) ? 0xae : 0x00;
		}

		if (reg == 0x09 && ((phyregs[0x07] >> 5) & 0x7) == 0)
			return ((phyregs[0x07] & 0xf) == 0) ? 0x40 : 0x00;

		return phyregs[reg];
	}

	void InitializePhyRegisters()
	{
		std::memset(phyregs, 0, sizeof(phyregs));
		phyregs[0x00] = static_cast<u8>((LOCAL_PHY_ID << 2) | 0x03); // physical ID, root, powered.
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
		QueueDbufR0Rx(1); // End marker consumed by the self-ID parser.
	}

	void TriggerBusReset()
	{
		phyregs[0x00] = static_cast<u8>((LOCAL_PHY_ID << 2) | 0x03);
		fwRu32(FW_NODE_ID) = LOCAL_NODE_ID_REGISTER;
		fwRu32(FW_CTRL0) = (fwRu32(FW_CTRL0) | FW_CTRL0_Root) & ~FW_CTRL0_BusIDRst;

		s_ubuf_rx_fifo.clear();
		s_dbuf_r0_rx_fifo.clear();
		QueueSelfIdPacket();
		if (s_active_device)
			s_active_device->BusReset();
		RaiseIntr0(FW_INTR0_PhyRst | FW_INTR0_DRFR);
		if (ShouldLogLimited(s_bus_reset_log_count, FW_BUS_RESET_LOG_LIMIT))
			DevCon.WriteLn("FW HLE: bus reset/self-ID local_phy=%u local_node=0x%04x remote_phy=%u", LOCAL_PHY_ID, LOCAL_NODE_ID, REMOTE_PHY_ID);
	}

	void MaybeRaisePendingInterrupts()
	{
		if ((fwRu32(FW_INTR0) & fwRu32(FW_INTR0_MASK)) || (fwRu32(FW_INTR1) & fwRu32(FW_INTR1_MASK)) ||
			(fwRu32(FW_INTR2) & fwRu32(FW_INTR2_MASK)))
			fwIrq();
	}

	void QueueWriteResponse(u32 request_header, u32 request_offset_high)
	{
		const u32 tlabel = (request_header >> 10) & 0x3f;
		const u32 dest_node = request_offset_high >> 16;
		const u32 bus_id = dest_node >> 6;
		if (FW_VERBOSE_LOGS)
			DevCon.WriteLn("FW HLE: queue UBUF WRITE_RESPONSE req_hdr=0x%x tlabel=0x%x dest_node=0x%x bus=0x%x", request_header, tlabel, dest_node, bus_id);

		QueueUbufRx((bus_id << 22) | (REMOTE_RESPONSE_SPEED << 16) | (tlabel << 10) | (1u << 8) | (IEEE1394_TCODE_WRITE_RESPONSE << 4));
		QueueUbufRx((dest_node << 16));
		QueueUbufRx(0);
		QueueUbufRx(1);
		RaiseIntr0(FW_INTR0_URx);
	}

	void AckTransmit(u32 ack)
	{
		fwRu32(FW_ACK_STAT) = ack << 28;
		if (FW_VERBOSE_LOGS)
			DevCon.WriteLn("FW HLE: tx ack ack=0x%x ack_status=0x%x", ack, fwRu32(FW_ACK_STAT));
		RaiseIntr0(FW_INTR0_AckRcvd);
	}

	void QueueReadResponse(u32 request_header, u16 dest_node, u32 value)
	{
		const u32 tlabel = (request_header >> 10) & 0x3f;
		const u32 bus_id = dest_node >> 6;
		if (FW_VERBOSE_LOGS)
			DevCon.WriteLn("FW HLE: queue UBUF READQ_RESPONSE req_hdr=0x%x tlabel=0x%x dest_node=0x%x bus=0x%x value=0x%x", request_header, tlabel, dest_node, bus_id, value);

		QueueUbufRx((bus_id << 22) | (REMOTE_RESPONSE_SPEED << 16) | (tlabel << 10) | (1u << 8) | (IEEE1394_TCODE_READQ_RESPONSE << 4));
		QueueUbufRx((dest_node << 16));
		QueueUbufRx(0);
		QueueUbufRx(value);
		QueueUbufRx(1);
		RaiseIntr0(FW_INTR0_URx);
	}

	void QueuePendingDbufQuadWrite(u32 offset_high, u32 offset_low, u32 payload)
	{
		if (ShouldLogLimited(s_remote_write_log_count, FW_REMOTE_WRITE_LOG_LIMIT))
			DevCon.WriteLn("FW HLE: queue DBUF WRITEQ off_hi=0x%x off_low=0x%x payload=0x%x", offset_high, offset_low, payload);
		s_pending_dbuf_r0_rx_dma.emplace_back();
		s_pending_dbuf_r0_rx_fifo.push_back((0x3ffu << 22) | (REMOTE_RESPONSE_SPEED << 16) | (1u << 10) | (IEEE1394_TCODE_WRITEQ << 4));
		s_pending_dbuf_r0_rx_fifo.push_back((LOCAL_NODE_ID << 16) | offset_high);
		s_pending_dbuf_r0_rx_fifo.push_back(offset_low);
		s_pending_dbuf_r0_rx_fifo.push_back(payload);
		s_pending_dbuf_r0_rx_fifo.push_back(2);
	}

	void QueuePendingDbufBlockWrite(u32 offset_high, u32 offset_low, const u32* payload, u32 payload_quads)
	{
		const u32 byte_count = payload_quads * sizeof(u32);
		if (ShouldLogLimited(s_remote_write_log_count, FW_REMOTE_WRITE_LOG_LIMIT))
			DevCon.WriteLn("FW HLE: queue DBUF WRITEB off_hi=0x%x off_low=0x%x bytes=0x%x", offset_high, offset_low, byte_count);
		s_pending_dbuf_r0_rx_dma.emplace_back();
		if (offset_high == 0x1000 && byte_count != 0)
		{
			PendingDbufDmaWrite& dma = s_pending_dbuf_r0_rx_dma.back();
			dma.active = true;
			dma.dest = offset_low;
			dma.data.resize(byte_count);
			std::memcpy(dma.data.data(), payload, byte_count);
		}
		s_pending_dbuf_r0_rx_fifo.push_back((0x3ffu << 22) | (REMOTE_RESPONSE_SPEED << 16) | (IEEE1394_TCODE_WRITEB << 4));
		s_pending_dbuf_r0_rx_fifo.push_back((LOCAL_NODE_ID << 16) | offset_high);
		s_pending_dbuf_r0_rx_fifo.push_back(offset_low);
		s_pending_dbuf_r0_rx_fifo.push_back(byte_count << 16);
		for (u32 i = 0; i < payload_quads; i++)
			s_pending_dbuf_r0_rx_fifo.push_back(payload[i]);
		s_pending_dbuf_r0_rx_fifo.push_back(2);
	}

	void QueuePendingDbufByteWrite(u32 offset_high, u32 offset_low, const u8* payload, u32 byte_count)
	{
		if (ShouldLogLimited(s_remote_write_log_count, FW_REMOTE_WRITE_LOG_LIMIT))
			DevCon.WriteLn("FW HLE: queue DBUF WRITEB off_hi=0x%x off_low=0x%x bytes=0x%x", offset_high, offset_low, byte_count);

		s_pending_dbuf_r0_rx_dma.emplace_back();
		if (offset_high == 0x1000 && byte_count != 0)
		{
			PendingDbufDmaWrite& dma = s_pending_dbuf_r0_rx_dma.back();
			dma.active = true;
			dma.dest = offset_low;
			dma.data.assign(payload, payload + byte_count);
		}

		s_pending_dbuf_r0_rx_fifo.push_back((0x3ffu << 22) | (REMOTE_RESPONSE_SPEED << 16) | (IEEE1394_TCODE_WRITEB << 4));
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
			if (FW_VERBOSE_LOGS)
				DevCon.WriteLn("FW HLE: FireWire DMA WRITEB off_hi=0x1000 off_low=0x%x bytes=0x%zx ok=%u", dma.dest, dma.data.size(), dma_written ? 1 : 0);
		}

		for (size_t i = 0; i < packet_quads; i++)
			QueueDbufR0Rx(s_pending_dbuf_r0_rx_fifo[i]);
		s_pending_dbuf_r0_rx_fifo.erase(s_pending_dbuf_r0_rx_fifo.begin(), s_pending_dbuf_r0_rx_fifo.begin() + packet_quads);
		if (!s_pending_dbuf_r0_rx_dma.empty())
			s_pending_dbuf_r0_rx_dma.erase(s_pending_dbuf_r0_rx_dma.begin());
		if (FW_VERBOSE_LOGS)
			DevCon.WriteLn("FW HLE: queued deferred DBUF R0 rx packet quads=0x%zx level=0x%x pending_quads=0x%zx pending_dma=0x%zx", packet_quads, fwRu32(FW_DBUF_FIFO_LV0), s_pending_dbuf_r0_rx_fifo.size(), s_pending_dbuf_r0_rx_dma.size());
		RaiseIntr0(FW_INTR0_URx);
	}

	class ControllerDeviceHost final : public FireWire::FireWireDeviceHost
	{
	public:
		u64 CurrentCycle() const override
		{
			return psxRegs.cycle;
		}

		void ScheduleEvent(u64 cycles) override
		{
			PSX_INT(IopEvt_FW, static_cast<s32>(std::min<u64>(std::max<u64>(cycles, 1), 0x7fffffffu)));
		}

		void ClearEvent() override
		{
			psxRegs.interrupt &= ~(1 << IopEvt_FW);
		}

		bool ReadIopMemory(u32 address, void* data, u32 size) override
		{
			return iopMemSafeReadBytes(address, data, size);
		}

		bool WriteIopMemory(u32 address, const void* data, u32 size) override
		{
			return iopMemSafeWriteBytes(address, data, size);
		}

		void QueueRemoteAsyncWriteQuad(u32 offset_high, u32 offset_low, u32 payload) override
		{
			QueuePendingDbufQuadWrite(offset_high, offset_low, payload);
		}

		void QueueRemoteAsyncWriteBlock(u32 offset_high, u32 offset_low, const u32* payload, u32 payload_quads) override
		{
			QueuePendingDbufBlockWrite(offset_high, offset_low, payload, payload_quads);
		}

		void QueueRemoteAsyncWriteBytes(u32 offset_high, u32 offset_low, const u8* payload, u32 byte_count) override
		{
			QueuePendingDbufByteWrite(offset_high, offset_low, payload, byte_count);
		}

		void FlushPendingRemoteWrites() override
		{
			FlushPendingDbufR0RxPacket();
		}
	};

	ControllerDeviceHost s_device_host;

	void ServiceActiveDeviceEvents()
	{
		if (s_active_device)
			s_active_device->ServiceEvents();
		else
			psxRegs.interrupt &= ~(1 << IopEvt_FW);
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

	void LogRuntimePayloadPreview(const char* prefix, u64 offset, const u32* payload, u32 payload_quads, bool handled)
	{
		if (!FW_VERBOSE_LOGS || !ShouldLogLimited(s_remote_write_log_count, FW_REMOTE_WRITE_LOG_LIMIT))
			return;

		const u32 p0 = payload_quads > 0 ? payload[0] : 0;
		const u32 p1 = payload_quads > 1 ? payload[1] : 0;
		const u32 p2 = payload_quads > 2 ? payload[2] : 0;
		const u32 p3 = payload_quads > 3 ? payload[3] : 0;
		DevCon.WriteLn("FW HLE: %s off=0x%llx payload_quads=0x%x handled=%u payload=%08x %08x %08x %08x",
			prefix, offset, payload_quads, handled ? 1 : 0, p0, p1, p2, p3);
	}

	void LogPhtRequest(int channel)
	{
		const u32 base = channel == 0 ? FW_PHT_CTRL0 : FW_PHT_CTRL1;
		const u32 hdr0 = fwRu32(base + 0x08);
		const u32 hdr1 = fwRu32(base + 0x0c);
		const u32 hdr2 = fwRu32(base + 0x10);
		if (ShouldLogLimited(s_pht_log_count, FW_DISCOVERY_LOG_LIMIT))
		{
			DevCon.WriteLn("FW HLE: PHT%d hdr node=0x%x off_hi=0x%x off_low=0x%x tlabel=0x%x speed=0x%x bytes=0x%x ctrl=0x%x",
				channel, hdr0 >> 16, hdr0 & 0xffff, hdr1, (hdr2 >> 19) & 0x3f, (hdr2 >> 16) & 0x7, hdr2 & 0xffff, fwRu32(base));
		}
	}

	void TryProcessPhtWrite(int channel)
	{
		if (!s_pht_write_pending[channel])
			return;

		const u32 expected_bytes = s_pht_tx_expected_bytes[channel];
		if (expected_bytes == 0 || s_pht_tx_fifo[channel].size() * sizeof(u32) < expected_bytes)
			return;

		const u32 base = channel == 0 ? FW_PHT_CTRL0 : FW_PHT_CTRL1;
		const u32 hdr0 = fwRu32(base + 0x08);
		const u32 hdr1 = fwRu32(base + 0x0c);
		const u64 offset = ((hdr0 & 0xffffull) << 32) | hdr1;
		const u32 payload_quads = (expected_bytes + 3) >> 2;
		std::vector<u32> payload(payload_quads);
		for (u32 i = 0; i < payload_quads; i++)
			payload[i] = s_pht_tx_fifo[channel][i];

		const bool handled = s_active_device && s_active_device->Write(offset, payload.data(), payload_quads);
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
		const u32 base = channel == 0 ? FW_PHT_CTRL0 : FW_PHT_CTRL1;
		const bool is_write = (control & FW_PHT_CTRL_EWREQ) != 0;

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
		if (FW_VERBOSE_LOGS)
			DevCon.WriteLn("FW HLE: process UBUF tx quads=%zu hdr=0x%x tcode=0x%x(%s) tlabel=0x%x speed=0x%x",
				s_ubuf_tx_fifo.size(), header, tcode, TcodeName(tcode), (header >> 10) & 0x3f, (header >> 16) & 0x7);

		if ((tcode == IEEE1394_TCODE_WRITEQ || tcode == IEEE1394_TCODE_WRITEB) && s_ubuf_tx_fifo.size() >= 4)
		{
			const u64 offset = ((s_ubuf_tx_fifo[1] & 0xffffull) << 32) | s_ubuf_tx_fifo[2];
			const u32 payload_start = (tcode == IEEE1394_TCODE_WRITEQ) ? 3 : 4;
			const u32 payload_quads = (s_ubuf_tx_fifo.size() > payload_start) ? static_cast<u32>(s_ubuf_tx_fifo.size() - payload_start) : 0;

			bool handled = false;
			if (payload_quads != 0)
			{
				std::vector<u32> payload(payload_quads);
				for (u32 i = 0; i < payload_quads; i++)
					payload[i] = ByteSwap32(s_ubuf_tx_fifo[payload_start + i]);

				handled = s_active_device && s_active_device->Write(offset, payload.data(), payload_quads);
				LogRuntimePayloadPreview("UBUF write", offset, payload.data(), payload_quads, handled);
				if (handled)
					RaiseIntr0(FW_INTR0_PBCntR);
			}

			AckTransmit(IEEE1394_ACK_COMPLETE);
		}
		else if ((tcode == IEEE1394_TCODE_READQ || tcode == IEEE1394_TCODE_READB) && s_ubuf_tx_fifo.size() >= 3)
		{
			const u16 node = s_ubuf_tx_fifo[1] >> 16;
			const u64 offset = ((s_ubuf_tx_fifo[1] & 0xffffull) << 32) | s_ubuf_tx_fifo[2];
			u32 value = 0;
			const bool handled = s_active_device && s_active_device->ReadQuadlet(offset, &value);
			if (FW_VERBOSE_LOGS && ShouldLogLimited(s_discovery_log_count, FW_DISCOVERY_LOG_LIMIT))
			{
				DevCon.WriteLn("FW HLE: UBUF read hdr=0x%x tlabel=0x%x node=0x%x off=0x%llx value=0x%x handled=%u",
					header, (header >> 10) & 0x3f, node, offset, value, handled ? 1 : 0);
			}
			AckTransmit(IEEE1394_ACK_PEND);
			QueueReadResponse(header, node, value);
		}

		RaiseIntr1(FW_INTR1_UTD);
		s_ubuf_tx_fifo.clear();
		FlushPendingDbufR0RxPacket();
	}

	void logFwAction(u32 addr, u32 value, bool write)
	{
		if (!FW_VERBOSE_LOGS)
			return;

		DevCon.WriteLn("FW: %s 0x%x: 0x%x", write ? "write" : "read", addr, value);
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
	psxRegs.interrupt &= ~(1 << IopEvt_FW);
	for (int channel = 0; channel < 2; channel++)
	{
		s_pht_tx_fifo[channel].clear();
		s_pht_tx_expected_bytes[channel] = 0;
		s_pht_write_pending[channel] = false;
	}
	s_bus_reset_log_count = 0;
	s_discovery_log_count = 0;
	s_dbuf_log_count = 0;
	s_ubuf_log_count = 0;
	s_intr_log_count = 0;
	s_pht_log_count = 0;
	s_remote_write_log_count = 0;

	const FireWire::FireWireDeviceProxy* proxy = nullptr;
	s_active_device = FireWire::CreateConfiguredDevice(&proxy);
	s_active_device_proxy = proxy;
	FireWire::SetActiveDevice(s_active_device_proxy, s_active_device.get());
	if (s_active_device && !s_active_device->Open(s_device_host))
	{
		s_active_device->Close();
		s_active_device.reset();
		s_active_device_proxy = nullptr;
		FireWire::SetActiveDevice(nullptr, nullptr);
	}

	// Initializing our registers.
	fwregs = static_cast<s8*>(std::calloc(0x10000, 1));
	if (fwregs == nullptr)
	{
		DevCon.WriteLn("FW: Error allocating Memory");
		if (s_active_device)
		{
			s_active_device->Close();
			s_active_device.reset();
			s_active_device_proxy = nullptr;
			FireWire::SetActiveDevice(nullptr, nullptr);
		}
		return -1;
	}
	fwRu32(FW_NODE_ID) = LOCAL_NODE_ID_REGISTER;
	fwRu32(FW_CTRL2) = 0x8;
	UpdateUbufRxLevel();
	UpdateDbufR0RxLevel();
	return 0;
}

void FWclose()
{
	if (s_active_device)
	{
		s_active_device->Close();
		s_active_device.reset();
		s_active_device_proxy = nullptr;
		FireWire::SetActiveDevice(nullptr, nullptr);
	}
	s_pending_dbuf_r0_rx_fifo.clear();
	s_pending_dbuf_r0_rx_dma.clear();
	psxRegs.interrupt &= ~(1 << IopEvt_FW);
	std::free(fwregs);
	fwregs = nullptr;
}

void FWsectorReadStatusInterrupt()
{
	ServiceActiveDeviceEvents();
}

void FWmixSubboardAudio(s32* left, s32* right)
{
	if (s_active_device)
		s_active_device->MixAudio(left, right);
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

	bool readback = (PHYACC & FW_PHY_ACCESS_READ) > 0;
	PHYACC &= ~FW_PHY_ACCESS_WRITE;

	if (readback)
		PHYRead();
}

void PHYRead()
{
	u8 reg = (PHYACC >> 24) & 0xf;

	PHYACC &= ~(FW_PHY_ACCESS_READ | 0xfffu);
	PHYACC |= ReadPhyRegister(reg) | (reg << 8);
	RaiseIntr0(FW_INTR0_PhyRRx);
}

u32 FWread32(u32 addr)
{
	ServiceActiveDeviceEvents();

	u32 ret = 0;
	switch (addr)
	{
		case FW_NODE_ID:
			ret = fwRu32(addr);
			break;
		case FW_CTRL2:
			ret = fwRu32(addr);
			break;
		case FW_INTR0:
			ret = fwRu32(addr);
			break;
		case FW_UBUF_RX:
			ret = PopUbufRx();
			break;
		case FW_UBUF_RX_LVL:
			ret = static_cast<u32>(s_ubuf_rx_fifo.size());
			fwRu32(addr) = ret;
			break;
		case FW_DBUF_FIFO_LV0:
			ret = static_cast<u32>((s_dbuf_r0_rx_fifo.size() * sizeof(u32)) << 16);
			fwRu32(addr) = ret;
			break;
		case FW_DBUF_RX_DATA:
			ret = PopDbufR0Rx();
			break;
		case FW_REG_7C:
			ret = 0x10000001;
			break;
		default:
			ret = fwRu32(addr);
			break;
	}

	logFwAction(addr, ret, false);
	return ret;
}

void FWwrite32(u32 addr, u32 value)
{
	ServiceActiveDeviceEvents();

	logFwAction(addr, value, true);
	switch (addr)
	{
		case FW_NODE_ID:
			fwRu32(addr) = LOCAL_NODE_ID_REGISTER;
			break;
		case FW_PHY_ACCESS:
			fwRu32(addr) = value;
			if (value & FW_PHY_ACCESS_WRITE)
				PHYWrite();
			else if (value & FW_PHY_ACCESS_READ)
				PHYRead();
			break;
		case FW_CTRL0:
			fwRu32(addr) = value;
			fwRu32(addr) &= ~(FW_CTRL0_BusIDRst | FW_CTRL0_RxRst | FW_CTRL0_TxRst);
			fwRu32(addr) |= FW_CTRL0_Root;
			break;
		case FW_CTRL2:
			fwRu32(addr) = 0x8 | value & 0x2;
			break;
		case FW_INTR0:
		case FW_INTR1:
		case FW_INTR2:
			if (addr == FW_INTR0 && ShouldLogLimited(s_intr_log_count, FW_INTR_LOG_LIMIT))
				DevCon.WriteLn("FW HLE: intr0 ack value=0x%x before=0x%x mask=0x%x", value, fwRu32(addr), fwRu32(FW_INTR0_MASK));
			fwRu32(addr) &= ~value;
			break;
		case FW_INTR0_MASK:
		case FW_INTR1_MASK:
		case FW_INTR2_MASK:
			if (addr == FW_INTR0_MASK && value != 0)
				value |= FW_INTR0_PhyRst;
			fwRu32(addr) = value;
			MaybeRaisePendingInterrupts();
			break;
		case FW_UBUF_TX_NEXT:
			s_ubuf_tx_fifo.push_back(value);
			fwRu32(addr) = value;
			break;
		case FW_UBUF_TX_LAST:
			s_ubuf_tx_fifo.push_back(value);
			fwRu32(addr) = value;
			ProcessUbufTransmitPacket();
			break;
		case FW_UBUF_TX_CLR:
			s_ubuf_tx_fifo.clear();
			fwRu32(addr) = value;
			break;
		case FW_UBUF_RX_CLR:
			s_ubuf_rx_fifo.clear();
			UpdateUbufRxLevel();
			fwRu32(addr) = value;
			break;
		case FW_DBUF_FIFO_LV0:
			if (value & FW_DBUF_FIFO_RESET_RX)
				s_dbuf_r0_rx_fifo.clear();
			UpdateDbufR0RxLevel();
			fwRu32(addr) |= value & FW_DBUF_FIFO_RESET_TX;
			break;
		case FW_PHT_CTRL0:
		case FW_PHT_CTRL1:
			fwRu32(addr) = value & ~FW_PHT_CTRL_PHTRst;
			if (value & (FW_PHT_CTRL_EWREQ | FW_PHT_CTRL_ERREQ))
				BeginPhtRequest(addr == FW_PHT_CTRL0 ? 0 : 1, value);
			break;
		case FW_DBUF_TX_DATA0:
			s_pht_tx_fifo[0].push_back(value);
			fwRu32(addr) = value;
			fwRu32(FW_DBUF_FIFO_LV0) = static_cast<u32>(s_pht_tx_fifo[0].size() << 16);
			TryProcessPhtWrite(0);
			break;
		case FW_DBUF_TX_DATA1:
			s_pht_tx_fifo[1].push_back(value);
			fwRu32(addr) = value;
			fwRu32(FW_DBUF_FIFO_LV1) = static_cast<u32>(s_pht_tx_fifo[1].size() << 16);
			TryProcessPhtWrite(1);
			break;
		case FW_DMA_CTRL0:
		case FW_DMA_CTRL1:
			fwRu32(addr) = value;
			break;
		default:
			fwRu32(addr) = value;
			break;
	}
}
