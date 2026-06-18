// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "USB/usb-perfect-pool-camera/perfect-pool-camera.h"
#include "Host.h"
#include "IconsFontAwesome.h"
#include "StateWrapper.h"
#include "USB/qemu-usb/desc.h"
#include "USB/qemu-usb/qusb.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace usb_perfect_pool_camera
{
	namespace
	{
		struct PerfectPoolCameraState
		{
			USBDevice dev{};
			USBDesc desc{};
			USBDescDevice desc_dev{};

			std::array<u8, 0x10000> firmware_ram{};
			bool cpu_held_in_reset = true;
			bool firmware_download_seen = false;
		};

		static const USBDescStrings desc_strings = {
			"Cypress",
			"AN2235 EZ-USB FX Microcontroller",
			"Perfect Pool Camera",
			"USBIO",
		};

		static constexpr u8 perfect_pool_camera_dev_descriptor[] = {
			0x12,          // bLength
			0x01,          // bDescriptorType
			WBVAL(0x0100), // bcdUSB
			0x00,          // bDeviceClass
			0x00,          // bDeviceSubClass
			0x00,          // bDeviceProtocol
			0x40,          // bMaxPacketSize0
			WBVAL(0x0547), // idVendor: Cypress
			WBVAL(0x2235), // idProduct: AN2235 EZ-USB FX
			WBVAL(0x0001), // bcdDevice
			0x01,          // iManufacturer
			0x02,          // iProduct
			0x00,          // iSerialNumber
			0x01,          // bNumConfigurations
		};

		static constexpr u8 perfect_pool_camera_config_descriptor[] = {
			0x09,        // bLength
			0x02,        // bDescriptorType
			WBVAL(0x2e), // wTotalLength
			0x01,        // bNumInterfaces
			0x01,        // bConfigurationValue
			0x04,        // iConfiguration
			0x80,        // bmAttributes
			0xfa,        // bMaxPower

			0x09, // bLength
			0x04, // bDescriptorType
			0x00, // bInterfaceNumber
			0x02, // bAlternateSetting, matches the board firmware descriptor used by usbio.irx
			0x04, // bNumEndpoints
			0xff, // bInterfaceClass
			0x00, // bInterfaceSubClass
			0xff, // bInterfaceProtocol
			0x04, // iInterface

			0x07,        // bLength
			0x05,        // bDescriptorType
			0x01,        // bEndpointAddress OUT 1, interrupt writes
			0x03,        // bmAttributes interrupt
			WBVAL(0x10), // wMaxPacketSize
			0x01,        // bInterval

			0x07,        // bLength
			0x05,        // bDescriptorType
			0x82,        // bEndpointAddress IN 2, interrupt reads
			0x03,        // bmAttributes interrupt
			WBVAL(0x10), // wMaxPacketSize
			0x01,        // bInterval

			0x07,        // bLength
			0x05,        // bDescriptorType
			0x03,        // bEndpointAddress OUT 3, bulk writes
			0x02,        // bmAttributes bulk
			WBVAL(0x40), // wMaxPacketSize
			0x00,        // bInterval

			0x07,        // bLength
			0x05,        // bDescriptorType
			0x84,        // bEndpointAddress IN 4, bulk reads
			0x02,        // bmAttributes bulk
			WBVAL(0x40), // wMaxPacketSize
			0x00,        // bInterval
		};

		static void perfect_pool_camera_reset(USBDevice* dev)
		{
			PerfectPoolCameraState* s = USB_CONTAINER_OF(dev, PerfectPoolCameraState, dev);

			if (!s->firmware_download_seen)
			{
				s->cpu_held_in_reset = true;
				s->firmware_ram.fill(0);
			}
		}

		static void perfect_pool_camera_unrealize(USBDevice* dev)
		{
			PerfectPoolCameraState* s = USB_CONTAINER_OF(dev, PerfectPoolCameraState, dev);
			delete s;
		}

		static void perfect_pool_camera_handle_control(
			USBDevice* dev, USBPacket* p, int request, int value, int index, int length, u8* data)
		{
			PerfectPoolCameraState* s = USB_CONTAINER_OF(dev, PerfectPoolCameraState, dev);
			const int ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
			if (ret >= 0)
				return;

			switch (request)
			{
				case VendorDeviceOutRequest | 0xa0:
				{
					const u32 address = static_cast<u32>(value) & 0xffff;
					const u32 write_size = std::min<u32>(static_cast<u32>(std::max(length, 0)), static_cast<u32>(s->firmware_ram.size() - address));
					if (write_size != 0)
					{
						std::memcpy(s->firmware_ram.data() + address, data, write_size);
						s->firmware_download_seen = true;
					}

					// The Perfect Pool firmware maps the Cypress 8051 reset latch at 0x7f92.
					// Standard EZ-USB CPUCS at 0xe600 is accepted too.
					if ((address == 0x7f92 || address == 0xe600) && write_size >= 1)
					{
						const bool was_held_in_reset = s->cpu_held_in_reset;
						s->cpu_held_in_reset = (data[0] & 1) != 0;
						if (was_held_in_reset && !s->cpu_held_in_reset && s->firmware_download_seen && dev->port)
							usb_reattach(dev->port);
					}

					p->actual_length = write_size;
					break;
				}

				case VendorDeviceRequest | 0xa0:
				{
					const u32 address = static_cast<u32>(value) & 0xffff;
					const u32 read_size = std::min<u32>(static_cast<u32>(std::max(length, 0)), static_cast<u32>(s->firmware_ram.size() - address));
					if (read_size != 0)
						std::memcpy(data, s->firmware_ram.data() + address, read_size);
					p->actual_length = read_size;
					break;
				}

				default:
					p->status = USB_RET_STALL;
					break;
			}
		}

		static void perfect_pool_camera_handle_data(USBDevice* dev, USBPacket* p)
		{
			switch (p->pid)
			{
				case USB_TOKEN_IN:
				{
					std::array<u8, 0x800> data{};
					size_t size = 0;

					switch (p->ep->nr)
					{
						case 2:
							size = 0x10;
							break;
						case 4:
							size = 0x800;
							break;
						default:
							p->status = USB_RET_STALL;
							return;
					}

					usb_packet_copy(p, data.data(), std::min<size_t>(size, p->buffer_size));
					break;
				}

				case USB_TOKEN_OUT:
					if (p->ep->nr != 1 && p->ep->nr != 3)
						p->status = USB_RET_STALL;
					else
						p->actual_length = usb_packet_size(p);
					break;

				default:
					p->status = USB_RET_STALL;
					break;
			}
		}
	} // namespace

	USBDevice* PerfectPoolCameraDevice::CreateDevice(SettingsInterface& si, u32 port, u32 subtype) const
	{
		PerfectPoolCameraState* s = new PerfectPoolCameraState();
		s->desc.full = &s->desc_dev;
		s->desc.str = desc_strings;

		if (usb_desc_parse_dev(perfect_pool_camera_dev_descriptor, sizeof(perfect_pool_camera_dev_descriptor), s->desc, s->desc_dev) < 0)
			goto fail;
		if (usb_desc_parse_config(perfect_pool_camera_config_descriptor, sizeof(perfect_pool_camera_config_descriptor), s->desc_dev) < 0)
			goto fail;

		s->dev.speed = USB_SPEED_FULL;
		s->dev.klass.handle_attach = usb_desc_attach;
		s->dev.klass.handle_reset = perfect_pool_camera_reset;
		s->dev.klass.handle_control = perfect_pool_camera_handle_control;
		s->dev.klass.handle_data = perfect_pool_camera_handle_data;
		s->dev.klass.unrealize = perfect_pool_camera_unrealize;
		s->dev.klass.usb_desc = &s->desc;
		s->dev.klass.product_desc = s->desc.str[2];

		usb_desc_init(&s->dev);
		usb_ep_init(&s->dev);
		perfect_pool_camera_reset(&s->dev);
		return &s->dev;

	fail:
		perfect_pool_camera_unrealize(&s->dev);
		return nullptr;
	}

	const char* PerfectPoolCameraDevice::Name() const
	{
		return TRANSLATE_NOOP("USB", "Perfect Pool Camera");
	}

	const char* PerfectPoolCameraDevice::TypeName() const
	{
		return "perfectpoolcamera";
	}

	const char* PerfectPoolCameraDevice::IconName() const
	{
		return ICON_FA_CAMERA;
	}

	bool PerfectPoolCameraDevice::Freeze(USBDevice* dev, StateWrapper& sw) const
	{
		PerfectPoolCameraState* s = USB_CONTAINER_OF(dev, PerfectPoolCameraState, dev);

		if (!sw.DoMarker("PerfectPoolCameraState"))
			return false;

		sw.DoPODArray(s->firmware_ram.data(), s->firmware_ram.size());
		sw.Do(&s->cpu_held_in_reset);
		sw.Do(&s->firmware_download_seen);
		return !sw.HasError();
	}
} // namespace usb_perfect_pool_camera
