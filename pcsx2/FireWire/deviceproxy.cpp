// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "FireWire/deviceproxy.h"

#include "FireWire/Devices/KonamiPython1.h"

#include "Host.h"

#include "common/SettingsInterface.h"

#include <algorithm>

namespace FireWire
{
	namespace
	{
		constexpr const char* FIREWIRE_CONFIG_SECTION = "FireWire";
		constexpr const char* FIREWIRE_DEVICE_KEY = "Device";
		constexpr const char* FIREWIRE_DEVICE_NONE = "None";
		constexpr const char* FIREWIRE_DEFAULT_DEVICE = FIREWIRE_DEVICE_NONE;
		constexpr const char* FIREWIRE_P1IO_DEVICE = "KonamiPython1";

		const FireWireDeviceProxy* s_active_device_proxy = nullptr;
		FireWireDevice* s_active_device = nullptr;
		std::string s_config_device_override;

		bool IsP1IOUniversalBinding(std::string_view bind_name)
		{
			return bind_name == "Test" || bind_name == "Service" || bind_name == "Coin1" || bind_name == "Coin2";
		}
	}

	RegisterDevice* RegisterDevice::s_register_device = nullptr;

	FireWireDeviceHost::~FireWireDeviceHost() = default;
	FireWireDevice::~FireWireDevice() = default;
	FireWireDeviceProxy::~FireWireDeviceProxy() = default;

	void FireWireDevice::BusReset()
	{
	}

	void FireWireDevice::Reset()
	{
	}

	void FireWireDevice::ServiceEvents()
	{
	}

	void FireWireDevice::MixAudio(s32* left, s32* right)
	{
	}

	std::span<const InputBindingInfo> FireWireDeviceProxy::Bindings() const
	{
		return {};
	}

	std::span<const SettingInfo> FireWireDeviceProxy::Settings() const
	{
		return {};
	}

	std::string FireWireDeviceProxy::BindingConfigKey(std::string_view bind_name) const
	{
		std::string key(TypeName());
		key.push_back('_');
		key.append(bind_name);
		return key;
	}

	bool FireWireDeviceProxy::MapAutomaticBindings(SettingsInterface& si, const std::vector<std::pair<GenericInputBinding, std::string>>& mapping) const
	{
		u32 num_mappings = 0;
		for (const InputBindingInfo& bi : Bindings())
		{
			if (bi.generic_mapping == GenericInputBinding::Unknown)
				continue;

			const auto found = std::find_if(mapping.begin(), mapping.end(), [generic = bi.generic_mapping](const auto& entry) {
				return entry.first == generic;
			});
			const std::string key = BindingConfigKey(bi.name);
			if (found != mapping.end())
			{
				si.SetStringValue(FIREWIRE_CONFIG_SECTION, key.c_str(), found->second.c_str());
				num_mappings++;
			}
			else
			{
				si.DeleteValue(FIREWIRE_CONFIG_SECTION, key.c_str());
			}
		}

		return num_mappings > 0;
	}

	void FireWireDeviceProxy::ClearBindings(SettingsInterface& si) const
	{
		for (const InputBindingInfo& bi : Bindings())
			si.DeleteValue(FIREWIRE_CONFIG_SECTION, BindingConfigKey(bi.name).c_str());
	}

	void FireWireDeviceProxy::CopyConfiguration(SettingsInterface* dest_si, const SettingsInterface& src_si, bool copy_bindings) const
	{
		if (!copy_bindings)
			return;

		for (const InputBindingInfo& bi : Bindings())
			dest_si->CopyStringValue(src_si, FIREWIRE_CONFIG_SECTION, BindingConfigKey(bi.name).c_str());
	}

	float FireWireDeviceProxy::GetBindingValue(const FireWireDevice* dev, u32 bind_index) const
	{
		return 0.0f;
	}

	void FireWireDeviceProxy::SetBindingValue(FireWireDevice* dev, u32 bind_index, float value) const
	{
	}

	void FireWireDeviceProxy::ResetBindingState(FireWireDevice* dev) const
	{
	}

	bool FireWireDeviceProxy::Freeze(FireWireDevice* dev, StateWrapper& sw) const
	{
		return false;
	}

	RegisterDevice& RegisterDevice::instance()
	{
		if (!s_register_device)
			s_register_device = new RegisterDevice();
		return *s_register_device;
	}

	void RegisterDevice::Register()
	{
		RegisterDevice& inst = RegisterDevice::instance();
		if (!inst.Map().empty())
			return;

		inst.Add(DEVTYPE_KONAMI_PYTHON1, new Devices::KonamiPython1DeviceProxy());
	}

	void RegisterDevice::Unregister()
	{
		m_register_device_map.clear();
		delete s_register_device;
		s_register_device = nullptr;
	}

	void RegisterDevice::Add(DeviceType key, FireWireDeviceProxy* creator)
	{
		m_register_device_map[key] = std::unique_ptr<FireWireDeviceProxy>(creator);
	}

	FireWireDeviceProxy* RegisterDevice::Device(std::string_view name)
	{
		Register();
		auto proxy = std::find_if(m_register_device_map.begin(), m_register_device_map.end(), [&name](const RegisterDeviceMap::value_type& val) {
			return val.second->TypeName() == name;
		});
		return proxy != m_register_device_map.end() ? proxy->second.get() : nullptr;
	}

	FireWireDeviceProxy* RegisterDevice::Device(s32 index)
	{
		Register();
		const auto it = m_register_device_map.find(static_cast<DeviceType>(index));
		return it != m_register_device_map.end() ? it->second.get() : nullptr;
	}

	DeviceType RegisterDevice::Index(std::string_view name)
	{
		Register();
		auto proxy = std::find_if(m_register_device_map.begin(), m_register_device_map.end(), [&name](const RegisterDeviceMap::value_type& val) {
			return val.second->TypeName() == name;
		});
		return proxy != m_register_device_map.end() ? proxy->first : DEVTYPE_NONE;
	}

	const RegisterDevice::RegisterDeviceMap& RegisterDevice::Map() const
	{
		return m_register_device_map;
	}

	const char* GetConfigSection()
	{
		return FIREWIRE_CONFIG_SECTION;
	}

	std::string GetConfigDevice(const SettingsInterface& si)
	{
		std::string device = si.GetStringValue(FIREWIRE_CONFIG_SECTION, FIREWIRE_DEVICE_KEY, FIREWIRE_DEFAULT_DEVICE);
		if (device.empty())
			device = FIREWIRE_DEFAULT_DEVICE;
		return device;
	}

	void SetConfigDevice(SettingsInterface& si, const char* devname)
	{
		si.SetStringValue(FIREWIRE_CONFIG_SECTION, FIREWIRE_DEVICE_KEY, devname ? devname : FIREWIRE_DEFAULT_DEVICE);
	}

	void SetConfigDeviceOverride(const char* devname)
	{
		s_config_device_override = devname ? devname : "";
	}

	static const FireWireDeviceProxy* GetDefaultDeviceProxy()
	{
		return RegisterDevice::instance().Device(FIREWIRE_DEFAULT_DEVICE);
	}

	static const FireWireDeviceProxy* GetP1IODeviceProxy()
	{
		return RegisterDevice::instance().Device(FIREWIRE_P1IO_DEVICE);
	}

	static const FireWireDeviceProxy* GetConfiguredDeviceProxy(const SettingsInterface* si)
	{
		const std::string device = !s_config_device_override.empty() ? s_config_device_override :
			(si ? GetConfigDevice(*si) : Host::GetStringSettingValue(FIREWIRE_CONFIG_SECTION, FIREWIRE_DEVICE_KEY, FIREWIRE_DEFAULT_DEVICE));
		if (device == FIREWIRE_DEVICE_NONE)
			return nullptr;

		const FireWireDeviceProxy* proxy = RegisterDevice::instance().Device(device);
		return proxy ? proxy : GetDefaultDeviceProxy();
	}

	std::string GetConfigSubKey(std::string_view bind_name)
	{
		const FireWireDeviceProxy* proxy = s_active_device_proxy ? s_active_device_proxy : GetDefaultDeviceProxy();
		return proxy ? proxy->BindingConfigKey(bind_name) : std::string(bind_name);
	}

	std::string GetConfigSubKey(const SettingsInterface& si, std::string_view bind_name)
	{
		const FireWireDeviceProxy* proxy = GetConfiguredDeviceProxy(&si);
		if (proxy && proxy->TypeName() == std::string_view(FIREWIRE_P1IO_DEVICE))
		{
			if (IsP1IOUniversalBinding(bind_name))
				return GetP1IOUniversalConfigSubKey(bind_name);

			return GetP1IOConfigSubKey(si, bind_name);
		}

		return proxy ? proxy->BindingConfigKey(bind_name) : std::string(bind_name);
	}

	std::vector<std::pair<const char*, const char*>> GetDeviceTypes()
	{
		RegisterDevice::Register();

		std::vector<std::pair<const char*, const char*>> ret;
		ret.emplace_back(FIREWIRE_DEVICE_NONE, "None");
		for (const auto& it : RegisterDevice::instance().Map())
			ret.emplace_back(it.second->TypeName(), it.second->Name());
		return ret;
	}

	const char* GetDeviceName(std::string_view device)
	{
		if (device == FIREWIRE_DEVICE_NONE)
			return "None";

		const FireWireDeviceProxy* proxy = RegisterDevice::instance().Device(device);
		return proxy ? proxy->Name() : "Unknown";
	}

	const char* GetDeviceIconName(std::string_view device)
	{
		const FireWireDeviceProxy* proxy = RegisterDevice::instance().Device(device);
		return proxy ? proxy->IconName() : "";
	}

	std::span<const InputBindingInfo> GetDeviceBindings(std::string_view device)
	{
		const FireWireDeviceProxy* proxy = RegisterDevice::instance().Device(device);
		return proxy ? proxy->Bindings() : std::span<const InputBindingInfo>();
	}

	std::span<const InputBindingInfo> GetSelectedDeviceBindings(const SettingsInterface& si)
	{
		const FireWireDeviceProxy* proxy = GetConfiguredDeviceProxy(&si);
		return proxy ? proxy->Bindings() : std::span<const InputBindingInfo>();
	}

	std::span<const SettingInfo> GetDeviceSettings(std::string_view device)
	{
		const FireWireDeviceProxy* proxy = RegisterDevice::instance().Device(device);
		return proxy ? proxy->Settings() : std::span<const SettingInfo>();
	}

	float GetDeviceBindValue(u32 bind_index)
	{
		return s_active_device_proxy ? s_active_device_proxy->GetBindingValue(s_active_device, bind_index) : 0.0f;
	}

	void SetDeviceBindValue(u32 bind_index, float value)
	{
		if (s_active_device_proxy)
			s_active_device_proxy->SetBindingValue(s_active_device, bind_index, value);
	}

	void ResetDeviceBindState()
	{
		if (s_active_device_proxy)
			s_active_device_proxy->ResetBindingState(s_active_device);
	}

	std::unique_ptr<FireWireDevice> CreateConfiguredDevice(const FireWireDeviceProxy** proxy)
	{
		const FireWireDeviceProxy* selected_proxy = GetConfiguredDeviceProxy(nullptr);
		if (proxy)
			*proxy = selected_proxy;
		return selected_proxy ? selected_proxy->CreateDevice() : nullptr;
	}

	void SetActiveDevice(const FireWireDeviceProxy* proxy, FireWireDevice* device)
	{
		s_active_device_proxy = proxy;
		s_active_device = device;
	}

	const FireWireDeviceProxy* GetActiveDeviceProxy()
	{
		return s_active_device_proxy;
	}

	std::span<const InputBindingInfo> GetP1IOBindings()
	{
		return GetDeviceBindings(FIREWIRE_P1IO_DEVICE);
	}

	float GetP1IOBindValue(u32 bind_index)
	{
		return GetDeviceBindValue(bind_index);
	}

	void SetP1IOBindValue(u32 bind_index, float value)
	{
		SetDeviceBindValue(bind_index, value);
	}

	void ResetP1IOBindState()
	{
		ResetDeviceBindState();
	}

	bool MapP1IO(SettingsInterface& si, const std::vector<std::pair<GenericInputBinding, std::string>>& mapping)
	{
		const FireWireDeviceProxy* proxy = GetP1IODeviceProxy();
		return proxy ? proxy->MapAutomaticBindings(si, mapping) : false;
	}

	void ClearP1IOBindings(SettingsInterface& si)
	{
		const FireWireDeviceProxy* proxy = GetP1IODeviceProxy();
		if (proxy)
			proxy->ClearBindings(si);
	}

	void CopyConfiguration(SettingsInterface* dest_si, const SettingsInterface& src_si, bool copy_bindings)
	{
		dest_si->CopyStringValue(src_si, FIREWIRE_CONFIG_SECTION, FIREWIRE_DEVICE_KEY);

		RegisterDevice::Register();
		for (const auto& it : RegisterDevice::instance().Map())
			it.second->CopyConfiguration(dest_si, src_si, copy_bindings);
	}

	void SetDefaultConfiguration(SettingsInterface* si)
	{
		si->ClearSection(FIREWIRE_CONFIG_SECTION);
	}
} // namespace FireWire
