// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#define _PC_	// disables MIPS opcode macros.

#include "R3000A.h"
#include "Common.h"
#include "IopHw.h"
#include "Sif.h"

#include <fmt/format.h>

#include <array>
#include <cstring>

void sifReset()
{
	std::memset(&sif0, 0, sizeof(sif0));
	std::memset(&sif1, 0, sizeof(sif1));
}

static bool SifIopFsTraceEnabled()
{
	static const bool enabled = std::getenv("PCSX2_IOP_FS_TRACE") != nullptr;
	return enabled;
}

static bool SifWe2K3NetTraceEnabled()
{
	static const bool enabled = std::getenv("PCSX2_WE2K3_NET_TRACE") != nullptr;
	return enabled;
}

static bool SifTraceInterestingSid(u32 sid)
{
	return (sid >= 0x80000590u && sid <= 0x8000059fu) ||
		(SifWe2K3NetTraceEnabled() && sid >= 0x10u && sid <= 0x17u);
}

static bool SifTracePrioritySid(u32 sid)
{
	return SifWe2K3NetTraceEnabled() &&
		((sid >= 0x80000590u && sid <= 0x8000059fu) || (sid >= 0x10u && sid <= 0x17u));
}

static std::string SifTraceAsciiFromBytes(const u8* bytes, int byte_count)
{
	std::string result;
	for (int i = 0; i < byte_count;)
	{
		while (i < byte_count && (bytes[i] < 0x20 || bytes[i] >= 0x7f))
			i++;

		const int start = i;
		while (i < byte_count && bytes[i] >= 0x20 && bytes[i] < 0x7f)
			i++;

		if (i - start >= 4)
		{
			if (!result.empty())
				result += ';';
			result.append(reinterpret_cast<const char*>(&bytes[start]), i - start);
			if (result.size() > 160)
			{
				result.resize(160);
				break;
			}
		}
	}

	return result;
}

static std::string SifTraceHexWords(const u32* words, int count)
{
	std::string head;
	const int head_words = std::min(count, 16);
	for (int i = 0; i < head_words; i++)
	{
		if (!head.empty())
			head += ' ';
		head += fmt::format("{:08x}", words[i]);
	}
	return head;
}

static std::string SifTraceIopPayload(u32 dest, u32 size)
{
	if (!dest || dest >= Ps2MemSize::ExposedIopRam || size == 0)
		return {};

	const u32 preview_size = std::min<u32>(size, 64);
	if (dest + preview_size > Ps2MemSize::ExposedIopRam)
		return {};

	const u8* payload = iopPhysMem(dest);
	std::string result = fmt::format(" payload_head=");
	for (u32 i = 0; i < preview_size; i++)
	{
		if (i != 0)
			result += ' ';
		result += fmt::format("{:02x}", payload[i]);
	}

	const std::string ascii = SifTraceAsciiFromBytes(payload, static_cast<int>(preview_size));
	if (!ascii.empty())
		result += fmt::format(" payload_ascii='{}'", ascii);
	return result;
}

static bool SifTraceIopPayloadHasPath(u32 dest, u32 size)
{
	if (!dest || dest >= Ps2MemSize::ExposedIopRam || size == 0)
		return false;

	const u32 preview_size = std::min<u32>(size, 512);
	if (dest + preview_size > Ps2MemSize::ExposedIopRam)
		return false;

	const char* payload = reinterpret_cast<const char*>(iopPhysMem(dest));
	static constexpr const char* needles[] = {
		"cdrom", "cdrom0", "cfc", "cfc0", "rom0", "host", "mc0", "ac0", "moduleload", "START",
	};
	for (const char* needle : needles)
	{
		const size_t needle_len = std::strlen(needle);
		for (u32 offset = 0; offset + needle_len <= preview_size; offset++)
		{
			if (std::memcmp(payload + offset, needle, needle_len) == 0)
				return true;
		}
	}

	return false;
}

struct SifTraceTrackedRpc
{
	u32 sid = 0;
	u32 cd = 0;
	u32 sd = 0;
};

static std::array<SifTraceTrackedRpc, 16> s_sif_trace_tracked_rpcs;

static SifTraceTrackedRpc* SifTraceFindByCd(u32 cd)
{
	if (!cd)
		return nullptr;

	for (SifTraceTrackedRpc& entry : s_sif_trace_tracked_rpcs)
	{
		if (entry.cd == cd)
			return &entry;
	}
	return nullptr;
}

static SifTraceTrackedRpc* SifTraceFindBySd(u32 sd)
{
	if (!sd)
		return nullptr;

	for (SifTraceTrackedRpc& entry : s_sif_trace_tracked_rpcs)
	{
		if (entry.sd == sd)
			return &entry;
	}
	return nullptr;
}

static SifTraceTrackedRpc* SifTraceTrack(u32 sid, u32 cd, u32 sd)
{
	if (SifTraceTrackedRpc* entry = SifTraceFindBySd(sd))
	{
		entry->sid = sid ? sid : entry->sid;
		entry->cd = cd ? cd : entry->cd;
		return entry;
	}

	if (SifTraceTrackedRpc* entry = SifTraceFindByCd(cd))
	{
		entry->sid = sid ? sid : entry->sid;
		entry->sd = sd ? sd : entry->sd;
		return entry;
	}

	for (SifTraceTrackedRpc& entry : s_sif_trace_tracked_rpcs)
	{
		if (entry.sid == 0)
		{
			entry.sid = sid;
			entry.cd = cd;
			entry.sd = sd;
			return &entry;
		}
	}

	s_sif_trace_tracked_rpcs[0] = {sid, cd, sd};
	return &s_sif_trace_tracked_rpcs[0];
}

void SifTraceRegisterRpc(u32 sid, u32 sd, u32 func, u32 buf, u32 cfunc, u32 cbuf, u32 qd)
{
	if ((!SifIopFsTraceEnabled() && !SifWe2K3NetTraceEnabled()) || !SifTraceInterestingSid(sid))
		return;

	SifTraceTrack(sid, 0, sd);
	Console.WriteLn("[IOPFS] RPC_REGISTER sid=0x%08x sd=0x%08x func=0x%08x buf=0x%08x cfunc=0x%08x cbuf=0x%08x qd=0x%08x",
		sid, sd, func, buf, cfunc, cbuf, qd);
}

static u32 SifTraceSidFromServerData(u32 sd)
{
	if (sd && sd + 4 <= Ps2MemSize::ExposedIopRam)
		return iopMemRead32(sd);
	return 0;
}

void SifTraceRpcPacket(const char* direction, u32 addr, const u32* words, int count, bool dest_is_iop)
{
	if (count < 4)
		return;

	const u32 psize = words[0] & 0xff;
	const u32 dsize = words[0] >> 8;
	const u32 dest = words[1];
	const u32 cid = words[2];
	const u32 opt = words[3];
	if (psize < 16 || psize > 112 || (psize & 3) != 0 || static_cast<int>(psize / 4) > count)
		return;

	static int log_count = 0;
	static int path_log_count = 0;
	static int priority_log_count = 0;

	const u32 SIF_CMD_RPC_END = 0x80000008u;
	const u32 SIF_CMD_RPC_BIND = 0x80000009u;
	const u32 SIF_CMD_RPC_CALL = 0x8000000au;
	const u32 SIF_CMD_RPC_RDATA = 0x8000000cu;

	if (!SifIopFsTraceEnabled() && !SifWe2K3NetTraceEnabled())
		return;

	if (cid == SIF_CMD_RPC_BIND && count >= 9)
	{
		const u32 cd = words[7];
		const u32 sid = words[8];
		if (!SifTraceInterestingSid(sid))
			return;

		SifTraceTrack(sid, cd, 0);
		const bool priority_sid = SifTracePrioritySid(sid);
		if (priority_sid)
		{
			if (priority_log_count >= 4096)
				return;
			priority_log_count++;
		}
		else if (log_count >= 2000)
			return;
		log_count++;
		Console.WriteLn("[IOPFS] %s RPC_BIND sid=0x%08x cd=0x%08x rec_id=0x%08x pkt=0x%08x rpc_id=0x%08x addr=0x%08x psize=0x%x dsize=0x%x dest=0x%08x opt=0x%08x head=%s%s",
			direction, sid, cd, words[4], words[5], words[6], addr, psize, dsize, dest, opt,
			SifTraceHexWords(words, count).c_str(), dest_is_iop ? SifTraceIopPayload(dest, dsize).c_str() : "");
		return;
	}

	if (cid == SIF_CMD_RPC_CALL && count >= 14)
	{
		const u32 cd = words[7];
		const u32 rpc_number = words[8];
		const u32 send_size = words[9];
		const u32 recvbuf = words[10];
		const u32 recv_size = words[11];
		const u32 rmode = words[12];
		const u32 sd = words[13];
		SifTraceTrackedRpc* tracked = SifTraceFindBySd(sd);
		u32 sid = tracked ? tracked->sid : 0;
		if (!tracked)
		{
			sid = SifTraceSidFromServerData(sd);
			if (SifTraceInterestingSid(sid))
				tracked = SifTraceTrack(sid, cd, sd);
		}
		if (!tracked)
			tracked = SifTraceFindByCd(cd);
		if (tracked)
			sid = tracked->sid;
		const bool path_payload = dest_is_iop && SifTraceIopPayloadHasPath(dest, dsize);
		if ((!tracked || !SifTraceInterestingSid(tracked->sid)) && !path_payload)
			return;
		const bool priority_sid = tracked && SifTracePrioritySid(tracked->sid);
		if (priority_sid)
		{
			if (priority_log_count >= 4096)
				return;
			priority_log_count++;
		}
		else if (path_payload)
		{
			if (path_log_count >= 4096)
				return;
			path_log_count++;
		}
		else if (log_count >= 2000)
		{
			return;
		}

		log_count++;
		Console.WriteLn("[IOPFS] %s %s sid=0x%08x rpc_number=0x%08x cd=0x%08x sd=0x%08x send=0x%x recv=0x%x recvbuf=0x%08x rmode=0x%08x rec_id=0x%08x pkt=0x%08x rpc_id=0x%08x addr=0x%08x psize=0x%x dsize=0x%x dest=0x%08x opt=0x%08x head=%s%s",
			direction, path_payload ? "RPC_CALL_PATH" : "RPC_CALL", sid, rpc_number, cd, sd, send_size, recv_size, recvbuf, rmode,
			words[4], words[5], words[6], addr, psize, dsize, dest, opt,
			SifTraceHexWords(words, count).c_str(), dest_is_iop ? SifTraceIopPayload(dest, dsize).c_str() : "");
		return;
	}

	if (cid == SIF_CMD_RPC_END && count >= 12)
	{
		const u32 cd = words[7];
		const u32 ended_cid = words[8];
		const u32 sd = words[9];
		SifTraceTrackedRpc* tracked = SifTraceFindByCd(cd);
		if (!tracked || !SifTraceInterestingSid(tracked->sid))
			return;

		if (ended_cid == SIF_CMD_RPC_BIND && sd)
			tracked->sd = sd;

		const bool priority_sid = SifTracePrioritySid(tracked->sid);
		if (priority_sid)
		{
			if (priority_log_count >= 4096)
				return;
			priority_log_count++;
		}
		else if (log_count >= 2000)
			return;
		log_count++;
		Console.WriteLn("[IOPFS] %s RPC_END sid=0x%08x ended_cid=0x%08x cd=0x%08x sd=0x%08x rec_id=0x%08x pkt=0x%08x rpc_id=0x%08x buf=0x%08x cbuf=0x%08x addr=0x%08x psize=0x%x dsize=0x%x dest=0x%08x opt=0x%08x head=%s%s",
			direction, tracked->sid, ended_cid, cd, sd, words[4], words[5], words[6], words[10], words[11],
			addr, psize, dsize, dest, opt, SifTraceHexWords(words, count).c_str(), dest_is_iop ? SifTraceIopPayload(dest, dsize).c_str() : "");
		return;
	}

	if (cid == SIF_CMD_RPC_RDATA && count >= 11)
	{
		const u32 recvbuf = words[7];
		const u32 src = words[8];
		const u32 rdest = words[9];
		const u32 size = words[10];
		if (!SifTraceInterestingSid(words[6]))
			return;

		const bool priority_sid = SifTracePrioritySid(words[6]);
		if (priority_sid)
		{
			if (priority_log_count >= 4096)
				return;
			priority_log_count++;
		}
		else if (log_count >= 2000)
			return;
		log_count++;
		Console.WriteLn("[IOPFS] %s RPC_RDATA rpc_id=0x%08x recvbuf=0x%08x src=0x%08x rdest=0x%08x size=0x%x addr=0x%08x psize=0x%x dsize=0x%x dest=0x%08x opt=0x%08x head=%s%s",
			direction, words[6], recvbuf, src, rdest, size, addr, psize, dsize, dest, opt,
			SifTraceHexWords(words, count).c_str(), dest_is_iop ? SifTraceIopPayload(dest, dsize).c_str() : "");
	}
}

void SifTraceCommandPacket(const char* direction, u32 addr, const u32* words, int count, bool dest_is_iop)
{
	if ((!SifIopFsTraceEnabled() && !SifWe2K3NetTraceEnabled()) || count < 4)
		return;

	const u32 psize = words[0] & 0xff;
	const u32 dsize = words[0] >> 8;
	const u32 dest = words[1];
	const u32 cid = words[2];
	const u32 opt = words[3];
	if (psize < 16 || psize > 112 || (psize & 3) != 0 || static_cast<int>(psize / 4) > count)
		return;

	if (cid >= 0x80000000u)
		return;

	static int log_count = 0;
	if (log_count >= 8192)
		return;

	const bool payload_interesting = dest_is_iop && SifTraceIopPayloadHasPath(dest, dsize);
	const bool uart_ee_request = dest_is_iop && cid == 0 && psize == 0x20 && dsize == 0 && dest == 0 && opt == 0 && count >= 8 && words[5] == 0x9600;
	const bool uart_iop_data = !dest_is_iop && cid == 0 && dsize == 0 && dest == 0 && opt == 0 && psize == 0x60 && count >= 8 && words[3] == 0;
	bool uart_iop_completion = false;
	if (!dest_is_iop && cid == 0 && dsize == 0 && dest == 0 && psize == 0x10 && (opt == 1 || opt == 2))
	{
		uart_iop_completion = true;
	}
	else if (!dest_is_iop && cid == 0 && dsize == 0 && dest == 0 && opt == 0 && psize >= 0x10 && psize <= 0x30)
	{
		for (int i = 4; i < std::min<int>(count, psize / 4); i++)
		{
			if (words[i] == 1 || words[i] == 2)
			{
				uart_iop_completion = true;
				break;
			}
		}
	}
	if (!payload_interesting && !uart_ee_request && !uart_iop_data && !uart_iop_completion)
		return;

	log_count++;
	const char* kind = uart_ee_request ? "UART_EE_REQ" : (uart_iop_data ? "UART_IOP_DATA" : (uart_iop_completion ? "UART_IOP_COMPLETE" : "CMD"));
	Console.WriteLn("[IOPFS] %s %s cid=0x%08x addr=0x%08x psize=0x%x dsize=0x%x dest=0x%08x opt=0x%08x head=%s%s",
		direction, kind, cid, addr, psize, dsize, dest, opt, SifTraceHexWords(words, count).c_str(),
		dest_is_iop ? SifTraceIopPayload(dest, dsize).c_str() : "");
}

bool SaveStateBase::sifFreeze()
{
	if (!FreezeTag("SIFdma"))
		return false;

	Freeze(sif0);
	Freeze(sif1);
	return IsOkay();
}
