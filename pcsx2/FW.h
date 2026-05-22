// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "Config.h"
#include "common/Pcsx2Defs.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

class SettingsInterface;

// Our main memory storage, and defines for accessing it.
extern s8* fwregs;
#define fwRs32(mem) (*(s32*)&fwregs[(mem)&0xffff])
#define fwRu32(mem) (*(u32*)&fwregs[(mem)&0xffff])

//PHY Access Address for ease of use :P
#define PHYACC fwRu32(0x8414)

s32 FWopen();
void FWclose();
void PHYWrite();
void PHYRead();
u32 FWread32(u32 addr);
void FWwrite32(u32 addr, u32 value);

namespace FireWire
{
	const char* GetConfigSection();
	std::string GetConfigSubKey(std::string_view bind_name);
	std::span<const InputBindingInfo> GetP1IOBindings();
	float GetP1IOBindValue(u32 bind_index);
	void SetP1IOBindValue(u32 bind_index, float value);
	void ResetP1IOBindState();
	bool MapP1IO(SettingsInterface& si, const std::vector<std::pair<GenericInputBinding, std::string>>& mapping);
	void ClearP1IOBindings(SettingsInterface& si);
	void CopyConfiguration(SettingsInterface* dest_si, const SettingsInterface& src_si, bool copy_bindings = true);
	void SetDefaultConfiguration(SettingsInterface* si);
}
