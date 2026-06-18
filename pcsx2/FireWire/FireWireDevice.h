// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

namespace FireWire
{
	constexpr u64 CROM_BASE = 0xffff'f000'0400;

	class FireWireDeviceHost
	{
	public:
		virtual ~FireWireDeviceHost();

		virtual u64 CurrentCycle() const = 0;
		virtual void ScheduleEvent(u64 cycles) = 0;
		virtual void ClearEvent() = 0;

		virtual bool ReadIopMemory(u32 address, void* data, u32 size) = 0;
		virtual bool WriteIopMemory(u32 address, const void* data, u32 size) = 0;

		virtual void QueueRemoteAsyncWriteQuad(u32 offset_high, u32 offset_low, u32 payload) = 0;
		virtual void QueueRemoteAsyncWriteBlock(u32 offset_high, u32 offset_low, const u32* payload, u32 payload_quads) = 0;
		virtual void QueueRemoteAsyncWriteBytes(u32 offset_high, u32 offset_low, const u8* payload, u32 byte_count) = 0;
		virtual void FlushPendingRemoteWrites() = 0;
	};

	class FireWireDevice
	{
	public:
		virtual ~FireWireDevice();

		virtual bool Open(FireWireDeviceHost& host) = 0;
		virtual void Close() = 0;
		virtual void Reset();
		virtual void BusReset();
		virtual bool ReadQuadlet(u64 offset, u32* value) = 0;
		virtual bool Write(u64 offset, const u32* payload, u32 payload_quads) = 0;
		virtual void ServiceEvents();
		virtual void MixAudio(s32* left, s32* right);
	};
} // namespace FireWire
