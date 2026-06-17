// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "FireWire/deviceproxy.h"

namespace FireWire::Devices
{
	u32 GetKonamiPython1P1IOLatchByte();
	u32 GetKonamiPython1P1IOMemcardSlot();
	bool IsKonamiPython1P1IOSerialMode();

	class KonamiPython1DeviceProxy final : public FireWireDeviceProxy
	{
	public:
		const char* Name() const override;
		const char* TypeName() const override;
		const char* IconName() const override;
		std::unique_ptr<FireWireDevice> CreateDevice() const override;

		std::span<const InputBindingInfo> Bindings() const override;
		std::string BindingConfigKey(std::string_view bind_name) const override;
		bool MapAutomaticBindings(SettingsInterface& si, const std::vector<std::pair<GenericInputBinding, std::string>>& mapping) const override;
		void ClearBindings(SettingsInterface& si) const override;
		void CopyConfiguration(SettingsInterface* dest_si, const SettingsInterface& src_si, bool copy_bindings) const override;

		float GetBindingValue(const FireWireDevice* dev, u32 bind_index) const override;
		void SetBindingValue(FireWireDevice* dev, u32 bind_index, float value) const override;
		void ResetBindingState(FireWireDevice* dev) const override;
	};
} // namespace FireWire::Devices
