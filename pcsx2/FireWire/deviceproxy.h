// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "Config.h"
#include "FireWire/FireWireDevice.h"

#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class SettingsInterface;

namespace FireWire
{
	enum DeviceType : s32
	{
		DEVTYPE_NONE = -1,
		DEVTYPE_KONAMI_PYTHON1 = 0,
	};

	class FireWireDeviceProxy
	{
	public:
		virtual ~FireWireDeviceProxy();

		virtual const char* Name() const = 0;
		virtual const char* TypeName() const = 0;
		virtual const char* IconName() const = 0;
		virtual std::unique_ptr<FireWireDevice> CreateDevice() const = 0;

		virtual std::span<const InputBindingInfo> Bindings() const;
		virtual std::span<const SettingInfo> Settings() const;
		virtual std::string BindingConfigKey(std::string_view bind_name) const;
		virtual bool MapAutomaticBindings(SettingsInterface& si, const std::vector<std::pair<GenericInputBinding, std::string>>& mapping) const;
		virtual void ClearBindings(SettingsInterface& si) const;
		virtual void CopyConfiguration(SettingsInterface* dest_si, const SettingsInterface& src_si, bool copy_bindings) const;

		virtual float GetBindingValue(const FireWireDevice* dev, u32 bind_index) const;
		virtual void SetBindingValue(FireWireDevice* dev, u32 bind_index, float value) const;
		virtual void ResetBindingState(FireWireDevice* dev) const;
	};

	class RegisterDevice
	{
		RegisterDevice(const RegisterDevice&) = delete;
		RegisterDevice() = default;
		static RegisterDevice* s_register_device;

	public:
		using RegisterDeviceMap = std::map<DeviceType, std::unique_ptr<FireWireDeviceProxy>>;

		static RegisterDevice& instance();
		static void Register();
		void Unregister();

		void Add(DeviceType key, FireWireDeviceProxy* creator);
		FireWireDeviceProxy* Device(std::string_view name);
		FireWireDeviceProxy* Device(s32 index);
		DeviceType Index(std::string_view name);
		const RegisterDeviceMap& Map() const;

	private:
		RegisterDeviceMap m_register_device_map;
	};

	const char* GetConfigSection();
	std::string GetConfigDevice(const SettingsInterface& si);
	void SetConfigDevice(SettingsInterface& si, const char* devname);
	std::string GetConfigSubKey(std::string_view bind_name);
	std::string GetConfigSubKey(const SettingsInterface& si, std::string_view bind_name);
	std::vector<std::pair<const char*, const char*>> GetDeviceTypes();
	const char* GetDeviceName(std::string_view device);
	const char* GetDeviceIconName(std::string_view device);
	std::span<const InputBindingInfo> GetDeviceBindings(std::string_view device);
	std::span<const InputBindingInfo> GetSelectedDeviceBindings(const SettingsInterface& si);
	std::span<const SettingInfo> GetDeviceSettings(std::string_view device);
	float GetDeviceBindValue(u32 bind_index);
	void SetDeviceBindValue(u32 bind_index, float value);
	void ResetDeviceBindState();
	std::unique_ptr<FireWireDevice> CreateConfiguredDevice(const FireWireDeviceProxy** proxy);
	void SetActiveDevice(const FireWireDeviceProxy* proxy, FireWireDevice* device);
	const FireWireDeviceProxy* GetActiveDeviceProxy();

	std::span<const InputBindingInfo> GetP1IOBindings();
	std::string GetP1IOConfigSubKey(std::string_view io_mode, std::string_view bind_name);
	std::string GetP1IOConfigSubKey(const SettingsInterface& si, std::string_view bind_name);
	std::string GetP1IOUniversalConfigSubKey(std::string_view bind_name);
	float GetP1IOBindValue(u32 bind_index);
	void SetP1IOBindValue(u32 bind_index, float value);
	void ResetP1IOBindState();
	bool MapP1IO(SettingsInterface& si, const std::vector<std::pair<GenericInputBinding, std::string>>& mapping);
	void ClearP1IOBindings(SettingsInterface& si);
	void CopyConfiguration(SettingsInterface* dest_si, const SettingsInterface& src_si, bool copy_bindings);
	void SetDefaultConfiguration(SettingsInterface* si);
} // namespace FireWire
