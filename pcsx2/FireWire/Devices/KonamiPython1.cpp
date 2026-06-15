// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "FireWire/Devices/KonamiPython1.h"

#include "Common.h"
#include "Host.h"

#include "Input/InputManager.h"

#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/SettingsInterface.h"
#include "common/StringUtil.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
	constexpr bool FW_VERBOSE_LOGS = false;
	constexpr u32 UART_RX_QUEUE_LIMIT = 0x400;
	constexpr u32 UART_CALLBACK_BYTE_LIMIT = 38;
	constexpr u64 KONAMI_STORAGE_READ_BYTES_PER_SECOND = 24ull * 1024 * 1024;
	constexpr u32 KONAMI_ADPCM_HEADER_SIZE = 0x10;
	constexpr u32 KONAMI_ADPCM_MAX_ENCODED_BYTES = 32 * 1024 * 1024;
	constexpr u32 KONAMI_ADPCM_INPUT_SAMPLE_RATE = 22050;
	constexpr u32 KONAMI_ADPCM_OUTPUT_SAMPLE_RATE = 48000;
	constexpr u32 KONAMI_ADPCM_FADE_SAMPLES = KONAMI_ADPCM_OUTPUT_SAMPLE_RATE / 200; // 5 ms.
	constexpr float KONAMI_ADPCM_LOWPASS_ALPHA = 0.60f;

	constexpr u32 KONAMI_CF_COMMAND_OFFSET = 0x390;
	constexpr u32 KONAMI_ATA_COMMAND_OFFSET = 0x3a0;
	constexpr u32 KONAMI_DALLAS_STATUS_OFFSET = 0x00010000;
	constexpr u32 KONAMI_UART_STATUS_OFFSET = 0x00030000;
	constexpr u32 KONAMI_CF_STATUS_OFFSET = 0x00050000;
	constexpr u32 KONAMI_ATA_STATUS_OFFSET = 0x00060000;
	constexpr u32 KONAMI_ADPCM_STATUS_OFFSET = 0x00070000;
	constexpr u32 KONAMI_BBSRAM_STATUS_OFFSET = 0x00080000;
	constexpr u32 KONAMI_BOOTROM_STATUS_OFFSET = 0x00090000;
	constexpr u32 KONAMI_FSCI_STATUS_OFFSET = 0x000a0000;
	constexpr u32 KONAMI_BBSRAM_NETWORK_ID_OFFSET = 0x686;
	constexpr u64 KONAMI_RUNTIME_READY_OFFSET = 0xfffd'0573'5730;
	constexpr u64 KONAMI_BOOT_READY_OFFSET = 0xfffd'0573'5734;
	constexpr u32 KONAMI_NET_COMMAND_OFFSET_BASE = 0x180;
	constexpr u32 KONAMI_NET_COMMAND_STRIDE = 0x20;
	constexpr u32 KONAMI_NET_RESPONSE_OFFSET_BASE = 0x000b0000;
	constexpr u32 KONAMI_NET_RESPONSE_STRIDE = 0x1000;
	constexpr u32 KONAMI_NET_CHANNEL_COUNT = 8;
	constexpr u32 KONAMI_JAMMA_INIT_COMMAND_OFFSET = 0x0e0;
	constexpr u32 KONAMI_JAMMA_OUTPUT_COMMAND_OFFSET = 0x0f80;
	constexpr u32 INVALID_JAMMA_INPUT_DEST = 0xffffffffu;
	constexpr u32 JAMMA_INPUT_REPORT_QUADS = 9;
	constexpr u32 P1IO_SOURCE_P1_START = 1u << 11;
	constexpr u32 P1IO_SOURCE_P1_UP = 1u << 2;
	constexpr u32 P1IO_SOURCE_P1_DOWN = 1u << 3;
	constexpr u32 P1IO_SOURCE_P1_LEFT = 1u << 0;
	constexpr u32 P1IO_SOURCE_P1_RIGHT = 1u << 1;
	constexpr u32 P1IO_SOURCE_P1_BUTTON1 = 1u << 4;
	constexpr u32 P1IO_SOURCE_P1_BUTTON2 = 1u << 5;
	constexpr u32 P1IO_SOURCE_P1_BUTTON3 = 1u << 6;
	constexpr u32 P1IO_SOURCE_P1_BUTTON4 = 1u << 7;
	constexpr u32 P1IO_SOURCE_P1_BUTTON5 = 1u << 8;
	constexpr u32 P1IO_SOURCE_P1_BUTTON6 = 1u << 9;
	constexpr u32 P1IO_SOURCE_P2_START = 1u << 27;
	constexpr u32 P1IO_SOURCE_P2_UP = 1u << 18;
	constexpr u32 P1IO_SOURCE_P2_DOWN = 1u << 19;
	constexpr u32 P1IO_SOURCE_P2_LEFT = 1u << 16;
	constexpr u32 P1IO_SOURCE_P2_RIGHT = 1u << 17;
	constexpr u32 P1IO_SOURCE_P2_BUTTON1 = 1u << 20;
	constexpr u32 P1IO_SOURCE_P2_BUTTON2 = 1u << 21;
	constexpr u32 P1IO_SOURCE_P2_BUTTON3 = 1u << 22;
	constexpr u32 P1IO_SOURCE_P2_BUTTON4 = 1u << 23;
	constexpr u32 P1IO_SOURCE_P2_BUTTON5 = 1u << 24;
	constexpr u32 P1IO_SOURCE_P2_BUTTON6 = 1u << 25;
	constexpr u32 P1IO_SOURCE_TEST = 1u << 15;
	constexpr u32 P1IO_SOURCE_SERVICE = 1u << 14;
	constexpr u32 SECTOR_SIZE = 0x200;
	constexpr u32 KONAMI_DBUF_WRITEB_MAX_PAYLOAD = 0x200;
	constexpr u32 BOOTROM_SIZE = 0x10000;
	constexpr u32 BBSRAM_SIZE = 0x2000;
	constexpr u32 KONAMI_BBSRAM_VOLATILE_TEST_BYTES = 0x1d00;
	constexpr u32 DALLAS_DONGLE_SLOT_COUNT = 2;
	constexpr u32 DALLAS_DONGLE_SERIAL_SIZE = 8;
	constexpr u32 DALLAS_DONGLE_PAYLOAD_SIZE = 0x20;
	constexpr u32 DALLAS_DONGLE_TOTAL_SIZE = DALLAS_DONGLE_SERIAL_SIZE + DALLAS_DONGLE_PAYLOAD_SIZE;
	constexpr u32 KONAMI_BOOTROM_MAC_BACKUP_OFFSET = 0xf000;
	constexpr const char* PYTHON1_GAME_CONFIG_SECTION = "Python1/Game";
	constexpr const char* PYTHON1_IO_MODE_JVS = "JVS";
	constexpr const char* PYTHON1_IO_MODE_EXTIO = "EXTIO";
	constexpr const char* PYTHON1_IO_MODE_POPN = "POPN";
	constexpr const char* PYTHON1_IO_MODE_DOGSTATION = "DOGSTATION";
	constexpr const char* P1IO_CONFIG_PREFIX = "P1IO_";
	constexpr u32 P1IO_KEYBOARD_BIND_BASE = 0x1000;
	// EE byte-swaps the JAMMA word, then maps source bit 8 to P1 bit 0x200.
	constexpr u32 JAMMA_P1_JVS_PRESENT = 0x00010000;
	// Captured neutral JAMMA status/DIP bytes. The present signal is in the input word above.
	constexpr u32 JAMMA_STATUS_NEUTRAL = 0x0100ffff;
	constexpr u32 JAMMA_ACTIVE_LOW_INPUT_MASK = 0x0000ffff;

	enum P1IOBinding : u32
	{
		P1IO_BIND_TEST,
		P1IO_BIND_SERVICE,
		P1IO_BIND_COIN1,
		P1IO_BIND_COIN2,
		P1IO_BIND_P1_START,
		P1IO_BIND_P1_UP,
		P1IO_BIND_P1_DOWN,
		P1IO_BIND_P1_LEFT,
		P1IO_BIND_P1_RIGHT,
		P1IO_BIND_P1_BUTTON1,
		P1IO_BIND_P1_BUTTON2,
		P1IO_BIND_P1_BUTTON3,
		P1IO_BIND_P1_BUTTON4,
		P1IO_BIND_P1_BUTTON5,
		P1IO_BIND_P1_BUTTON6,
		P1IO_BIND_P2_START,
		P1IO_BIND_P2_UP,
		P1IO_BIND_P2_DOWN,
		P1IO_BIND_P2_LEFT,
		P1IO_BIND_P2_RIGHT,
		P1IO_BIND_P2_BUTTON1,
		P1IO_BIND_P2_BUTTON2,
		P1IO_BIND_P2_BUTTON3,
		P1IO_BIND_P2_BUTTON4,
		P1IO_BIND_P2_BUTTON5,
		P1IO_BIND_P2_BUTTON6,
		P1IO_BIND_COUNT,
	};

	enum class Python1IOMode
	{
		JVS,
		EXTIO,
		POPN,
		DOGSTATION,
	};

	struct P1IOJammaMapping
	{
		u32 bind;
		u32 active_low_mask;
	};

	static constexpr InputBindingInfo P1IO_BINDINGS[] = {
		{"Test", "Test Button", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_TEST, GenericInputBinding::Select},
		{"Service", "Service Button", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_SERVICE, GenericInputBinding::System},
		{"Coin1", "Coin Player 1", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_COIN1, GenericInputBinding::L3},
		{"Coin2", "Coin Player 2", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_COIN2, GenericInputBinding::R3},
		{"P1Start", "P1 Start", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P1_START, GenericInputBinding::Start},
		{"P1Up", "P1 Up", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P1_UP, GenericInputBinding::DPadUp},
		{"P1Down", "P1 Down", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P1_DOWN, GenericInputBinding::DPadDown},
		{"P1Left", "P1 Left", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P1_LEFT, GenericInputBinding::DPadLeft},
		{"P1Right", "P1 Right", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P1_RIGHT, GenericInputBinding::DPadRight},
		{"P1Button1", "P1 Button 1", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P1_BUTTON1, GenericInputBinding::Cross},
		{"P1Button2", "P1 Button 2", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P1_BUTTON2, GenericInputBinding::Circle},
		{"P1Button3", "P1 Button 3", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P1_BUTTON3, GenericInputBinding::Square},
		{"P1Button4", "P1 Button 4", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P1_BUTTON4, GenericInputBinding::Triangle},
		{"P1Button5", "P1 Button 5", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P1_BUTTON5, GenericInputBinding::L1},
		{"P1Button6", "P1 Button 6", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P1_BUTTON6, GenericInputBinding::R1},
		{"P2Start", "P2 Start", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P2_START, GenericInputBinding::Unknown},
		{"P2Up", "P2 Up", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P2_UP, GenericInputBinding::Unknown},
		{"P2Down", "P2 Down", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P2_DOWN, GenericInputBinding::Unknown},
		{"P2Left", "P2 Left", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P2_LEFT, GenericInputBinding::Unknown},
		{"P2Right", "P2 Right", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P2_RIGHT, GenericInputBinding::Unknown},
		{"P2Button1", "P2 Button 1", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P2_BUTTON1, GenericInputBinding::Unknown},
		{"P2Button2", "P2 Button 2", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P2_BUTTON2, GenericInputBinding::Unknown},
		{"P2Button3", "P2 Button 3", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P2_BUTTON3, GenericInputBinding::Unknown},
		{"P2Button4", "P2 Button 4", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P2_BUTTON4, GenericInputBinding::Unknown},
		{"P2Button5", "P2 Button 5", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P2_BUTTON5, GenericInputBinding::Unknown},
		{"P2Button6", "P2 Button 6", nullptr, InputBindingInfo::Type::Button, P1IO_BIND_P2_BUTTON6, GenericInputBinding::Unknown},
		{"Keyboard1", "Keyboard Player 1", nullptr, InputBindingInfo::Type::Keyboard, P1IO_KEYBOARD_BIND_BASE, GenericInputBinding::Unknown},
	};

	// P1IO's input report is still being documented, so keep the bit layout centralized.
	// Neutral has the low 16 bits high; pressed inputs clear one bit.
	static constexpr P1IOJammaMapping P1IO_JAMMA_MAPPINGS[] = {
		{P1IO_BIND_TEST, 0x0001},
		{P1IO_BIND_SERVICE, 0x0002},
		{P1IO_BIND_COIN1, 0x0004},
		{P1IO_BIND_COIN2, 0x0008},
		{P1IO_BIND_P1_START, 0x0010},
		{P1IO_BIND_P1_UP, 0x0020},
		{P1IO_BIND_P1_DOWN, 0x0040},
		{P1IO_BIND_P1_LEFT, 0x0080},
		{P1IO_BIND_P1_RIGHT, 0x0100},
		{P1IO_BIND_P1_BUTTON1, 0x0200},
		{P1IO_BIND_P1_BUTTON2, 0x0400},
		{P1IO_BIND_P1_BUTTON3, 0x0800},
		{P1IO_BIND_P1_BUTTON4, 0x1000},
		{P1IO_BIND_P1_BUTTON5, 0x2000},
		{P1IO_BIND_P1_BUTTON6, 0x4000},
	};
	constexpr u8 KONAMI_FACTORY_MAC[] = {
		0x00, 0x04, 0x5f, 0x00, 0x00, 0x01,
	};
	constexpr u8 KONAMI_MAC_BACKUP[] = {
		0x00, 0x04, 0x5f, 0x00, 0x00, 0x01,
		0xff, 0xfb, 0xa0, 0xff, 0xff, 0xfe,
	};
	constexpr u8 KONAMI_NET_IP_ADDRESS[] = {192, 168, 50, 2};
	constexpr u8 KONAMI_NET_SUBNET_MASK[] = {255, 255, 255, 0};
	constexpr u8 KONAMI_NET_GATEWAY[] = {192, 168, 50, 1};
	constexpr u8 KONAMI_NET_PRIMARY_DNS[] = {192, 168, 50, 1};
	constexpr bool KONAMI_NET_FORCE_OFFLINE = true;
	constexpr u8 KONAMI_NET_OFFLINE_IP_ADDRESS[] = {169, 254, 50, 2};
	constexpr u8 KONAMI_NET_OFFLINE_GATEWAY[] = {0, 0, 0, 0};
	constexpr u8 KONAMI_NET_OFFLINE_DNS[] = {0, 0, 0, 0};
	constexpr u8 KONAMI_NET_MACHINE_ID[] = {
		'P', 'Y', 'T', 'H', 'O', 'N', '1', '-',
		'0', '0', '0', '0', '0', '0', '0', '1',
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
	};
	constexpr u8 KONAMI_NET_SECONDARY_DNS[] = {0, 0, 0, 0};
	// WE2K3 accepts a programmed backup Dallas key record when no new security key is inserted.
	constexpr u8 KONAMI_WE2K3_DALLAS_BOOTROM_RECORD[] = {
		0x00, 0x04, 0x5f, 0x00, 0x00, 0x01, 0x00, 0x00,
		0x8c, 0x02, 0x4d, 0x83, 0x06, 0xd2, 0x00, 0x00,
		'G', 'E', 'C', '2', '7', 0x00, 0x00, 0x00,
		0x20, 0x03, 'J', 'A', 'E', 0x00, 0x8f, 0x43,
	};
	constexpr u8 KONAMI_DEFAULT_DALLAS_ID[] = {
		0x5f, 0x04, 0x00, 0x01, 0x00, 0x01,
	};
	constexpr u32 KONAMI_DALLAS_NO_KEY_RESPONSE[] = {
		0x01000000, 0x00000000, 0x00000000,
	};
	constexpr u32 KONAMI_FSCI_MAC_FRAME_SIZE = 18;
	constexpr u32 KONAMI_FSCI_INITIAL_MAC_FRAMES = 9;
	constexpr u32 KONAMI_FSCI_INITIAL_MAC_STREAM_SIZE = KONAMI_FSCI_INITIAL_MAC_FRAMES * KONAMI_FSCI_MAC_FRAME_SIZE;
	constexpr u32 KONAMI_FSCI_MAX_READ_BYTES = KONAMI_DBUF_WRITEB_MAX_PAYLOAD;
	constexpr u8 KONAMI_EXTERNAL_IO_SYNC_RESPONSE[] = {
		0xaa, 0xaa, 0xaa, 0x55,
	};
	constexpr u8 KONAMI_EXTERNAL_IO_RESET_RESPONSE[] = {
		0xaa, 0xaa, 0x00, 0x00,
	};
	constexpr u8 KONAMI_EXTERNAL_IO_COUNT_RESPONSE[] = {
		0xaa, 0xaa, 0x00, 0x01, 0x01, 0x01, 0xad,
	};
	constexpr u8 KONAMI_DOGSTATION_EXTERNAL_IO_COUNT_RESPONSE[] = {
		0xaa, 0xaa, 0x00, 0x01, 0x01, 0x02, 0xae,
	};
	constexpr u8 KONAMI_EXTERNAL_IO_PRODUCT_RESPONSE[] = {
		0xaa, 0xaa, 0x01, 0x02, 0x00, 0x00,
		0xaa, 0x01, 0x00, 0x02, 0x00,
		0x03, 0x00, 0x00, 0x00,
		0x00,
		0x01, 0x01, 0x00,
		'I', 'C', 'C', 'A', 0x00, 0x00, 0x00, 0x00, 0x18,
	};
	constexpr u8 KONAMI_EXTERNAL_IO_STARTUP_RESPONSE[] = {
		0xaa, 0xaa, 0x01, 0x03, 0x00, 0x00,
		0xaa, 0x01, 0x00, 0x03, 0x00, 0x00, 0x04,
	};
	constexpr u8 KONAMI_ICCA_PRODUCT_RESPONSE[] = {
		0xaa, 0x81, 0x00, 0x02, 0x00, 0x2c,
		0x03, 0x00, 0x00, 0x00, // Device ID
		0x00, // Flag
		0x01, 0x01, 0x00, // Version
		'I', 'C', 'C', 'A',
		'O', 'c', 't', ' ', '2', '6', ' ', '2', '0', '0', '5', 0x00, 0x00, 0x00, 0x00, 0x00,
		'1', '3', ' ', ':', ' ', '5', '5', ' ', ':', ' ', '0', '3', 0x00, 0x00, 0x00, 0x00,
	};
	constexpr u8 KONAMI_ICCA_STARTUP_RESPONSE[] = {
		0xaa, 0x81, 0x00, 0x03, 0x00, 0x01, 0x00,
	};
	constexpr u8 KONAMI_ICCA_STATUS_RESPONSE[] = {
		0xaa, 0x81, 0x01, 0x31, 0x00, 0x10,
		0x01, 0x00, // Reader present, no card/sensor active.
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
	};
	constexpr s32 KONAMI_ADPCM_STEPS[] = {
		256, 272, 304, 336, 368, 400, 448, 496, 544, 592, 656, 720,
		800, 880, 960, 1056, 1168, 1280, 1408, 1552, 1712, 1888, 2080, 2288,
		2512, 2768, 3040, 3344, 3680, 4048, 4464, 4912, 5392, 5936, 6528, 7184,
		7904, 8704, 9568, 10528, 11584, 12736, 14016, 15408, 16960, 18656, 20512, 22576,
		24832,
	};
	constexpr s32 KONAMI_ADPCM_INDEX_CHANGES[] = {
		-1, -1, -1, -1, 2, 4, 6, 8,
		-1, -1, -1, -1, 2, 4, 6, 8,
	};

	struct PendingSectorStatusWrite
	{
		u64 ready_cycle = 0;
		u32 status_offset = 0;
		u32 checksum = 0;
	};

	struct PendingNetPacket
	{
		std::vector<u8> data;
		u32 source_ip = 0;
		u16 source_port = 0;
	};

	struct SubboardAdpcmPlayback
	{
		std::vector<u8> encoded;
		std::vector<std::array<s16, 2>> samples;
		std::array<s32, 2> decoder_step_index = {};
		std::array<s32, 2> decoder_pcm_sample = {};
		size_t position = 0;
		size_t loop_start = 0;
		size_t loop_end = 0;
		u32 resample_accumulator = 0;
		u32 volume_left = 0;
		u32 volume_right = 0;
		std::array<float, 2> filter_state = {};
		u32 fade_in_remaining = 0;
		u32 fade_out_remaining = 0;
		u32 sector = 0;
		bool playing = false;
		bool looping = false;
		bool stopping = false;
		bool filter_initialized = false;
	};

	struct Python1KeyboardKeyMapping
	{
		const char* host_name;
		u8 ps2_set2_make;
		bool extended = false;
	};

	struct Python1KeyboardKey
	{
		u8 ps2_set2_make = 0;
		bool extended = false;

		u32 Index() const { return static_cast<u32>(ps2_set2_make) | (extended ? 0x100u : 0); }
	};

	static constexpr Python1KeyboardKeyMapping PYTHON1_KEYBOARD_MAPPINGS[] = {
		{"A", 0x1c}, {"B", 0x32}, {"C", 0x21}, {"D", 0x23}, {"E", 0x24}, {"F", 0x2b}, {"G", 0x34},
		{"H", 0x33}, {"I", 0x43}, {"J", 0x3b}, {"K", 0x42}, {"L", 0x4b}, {"M", 0x3a}, {"N", 0x31},
		{"O", 0x44}, {"P", 0x4d}, {"Q", 0x15}, {"R", 0x2d}, {"S", 0x1b}, {"T", 0x2c}, {"U", 0x3c},
		{"V", 0x2a}, {"W", 0x1d}, {"X", 0x22}, {"Y", 0x35}, {"Z", 0x1a},
		{"0", 0x45}, {"1", 0x16}, {"2", 0x1e}, {"3", 0x26}, {"4", 0x25}, {"5", 0x2e}, {"6", 0x36},
		{"7", 0x3d}, {"8", 0x3e}, {"9", 0x46},
		{"Return", 0x5a}, {"Escape", 0x76}, {"Backspace", 0x66}, {"Tab", 0x0d}, {"Space", 0x29},
		{"Minus", 0x4e}, {"Equal", 0x55}, {"At", 0x54}, {"BracketLeft", 0x5b},
		{"BracketRight", 0x5d}, {"Semicolon", 0x4c}, {"Apostrophe", 0x52}, {"Agrave", 0x0e},
		{"Comma", 0x41}, {"Period", 0x49}, {"Slash", 0x4a},
		{"F1", 0x05}, {"F2", 0x06}, {"F3", 0x04}, {"F4", 0x0c}, {"F5", 0x03}, {"F6", 0x0b},
		{"F7", 0x83}, {"F8", 0x0a}, {"F9", 0x01}, {"F10", 0x09}, {"F11", 0x78}, {"F12", 0x07},
		{"Shift", 0x12}, {"Shift_r", 0x59}, {"Control", 0x14}, {"Alt", 0x11},
		{"Henkan", 0x64}, {"Muhenkan", 0x67}, {"Hiragana_Katakana", 0x13},
		{"Up", 0xf5}, {"Down", 0xf2}, {"Left", 0xeb}, {"Right", 0xf4},
		{"Insert", 0x70, true}, {"Delete", 0x71, true}, {"Home", 0x6c, true}, {"End", 0x69, true},
		{"PageUp", 0x7d, true}, {"PageDown", 0x7a, true},
		// {"NumpadReturn", 0x5a, true},
		{"CapsLock", 0x58}, {"Eisu_toggle", 0x58},
		{"Numpad7", 0xec, true}, {"Numpad8", 0xf5, true}, {"Numpad9", 0xfd, true},
		{"Numpad4", 0xeb, true}, {"Numpad5", 0xf3, true}, {"Numpad6", 0x74},
		{"Numpad1", 0xe9, true}, {"Numpad2", 0xf2, true}, {"Numpad3", 0xfa, true},
		{"Numpad0", 0x70}, {"NumpadEnter", 0xda},
		{"Zenkaku_Hankaku", 0x8e, true},
		{"AsciiCircum", 0x55},
		{"Colon", 0x52},
		{"yen", 0x6a},
		{"Backslash", 0x51},
		// {"Shift", 0xd9, true}, // right shift
		// {"Control", 0x94}, // right control
	};

	// Captured from the WE2K3 Python1 IO board's config-ROM reads in python1-boot-ioerror.nosy.
	constexpr u32 KONAMI_IO_BOARD_CROM[] = {
		0x0404a1a4, 0x31333934, 0x407d8002, 0x00000000, 0x00000000, 0x00053f04,
		0x03000679, 0x8100000a, 0x0c0083c0, 0xc3000005, 0xd1000001, 0x00020901,
		0x12000679, 0x13001000, 0x000231e6, 0x17000000, 0x81000006, 0x0004f2d6,
		0x00000000, 0x00000000, 0x4b4f4e41, 0x4d490000, 0x000428a6, 0x00000000,
		0x00000000, 0x5053322d, 0x41430000,
	};

	class KonamiPython1Device final : public FireWire::FireWireDevice
	{
	public:
		bool Open(FireWire::FireWireDeviceHost& host) override;
		void Close() override;
		bool ReadQuadlet(u64 offset, u32* value) override;
		bool Write(u64 offset, const u32* payload, u32 payload_quads) override;
		void ServiceEvents() override;
		void MixAudio(s32* left, s32* right) override;

		float GetBindingValue(u32 bind_index) const;
		void SetBindingValue(u32 bind_index, float value);
		void ResetBindingState();
		u32 GetBindingState() const;
		bool IsKeyboard1KeyPressed(Python1KeyboardKey key) const;
		u32 PopKeyboard1Events(u8* dest, u32 max_events);

	private:
		std::atomic<u32> m_p1io_bind_state{0};
		mutable std::mutex m_keyboard1_mutex;
		std::array<bool, 0x200> m_keyboard1_pressed = {};
		std::vector<u8> m_keyboard1_events;
	};

	FireWire::FireWireDeviceHost* s_host = nullptr;
	KonamiPython1Device* s_device = nullptr;
	std::vector<PendingSectorStatusWrite> s_pending_sector_status_writes;
	std::FILE* s_hdd_image_file = nullptr;
	std::string s_hdd_image_open_path;
	bool s_hdd_image_writable = false;
	bool s_pythonfs_formatted = false;
	std::vector<PendingNetPacket> s_net_rx_packets[KONAMI_NET_CHANNEL_COUNT];
	std::array<u8, 64> s_net_property_response;
	std::vector<u8> s_uart_rx_fifo;
	u8 s_bootrom[BOOTROM_SIZE];
	std::vector<u32> s_io_config_rom;
	u8 s_bbsram[BBSRAM_SIZE];
	std::array<std::array<u8, DALLAS_DONGLE_TOTAL_SIZE>, DALLAS_DONGLE_SLOT_COUNT> s_dallas_dongle_slots;
	std::array<bool, DALLAS_DONGLE_SLOT_COUNT> s_dallas_dongle_loaded;
	std::array<u8, sizeof(KONAMI_FACTORY_MAC)> s_factory_mac;
	std::array<u8, KONAMI_FSCI_INITIAL_MAC_STREAM_SIZE> s_fsci_mac_stream;
	u32 s_fsci_stream_offset;
	bool s_bootrom_dirty;
	bool s_bbsram_dirty;
	bool s_dallas_dongle_dirty[DALLAS_DONGLE_SLOT_COUNT];
	u32 s_jamma_input_dest = INVALID_JAMMA_INPUT_DEST;
	u32 s_last_jamma_input_status = JAMMA_STATUS_NEUTRAL;
	u32 s_last_p1io_bind_state = 0;
	u16 s_p1io_coin_counters[2] = {};
	u32 s_p1io_output_latch_byte = 0;
	u32 s_p1io_memcard_slot = 1;
	u64 s_next_sector_read_ready_cycle;
	SubboardAdpcmPlayback s_subboard_adpcm;

	u32 ByteSwap32(u32 value);
	u32 ReadBigEndian32(const u8* data);
	bool TryReadKonamiQuadlet(u64 offset, u32* value);
	bool TryHleKonamiCommand(u64 offset, const u32* payload, u32 payload_quads);
	void ServicePendingSectorStatusWrites();
	void MixSubboardAdpcmAudio(s32* left, s32* right);
	void SaveBootromIfDirty();
	void SaveBbsramIfDirty();
	void SaveDallasDongleIfDirty();
	void CloseHddImageFile();
	void ResetSubboardAdpcmPlayback();
	bool IsPython1DogstationMode();

	bool ReadIopMemory(u32 address, void* data, u32 size)
	{
		return s_host && s_host->ReadIopMemory(address, data, size);
	}

	bool WriteIopMemory(u32 address, const void* data, u32 size)
	{
		return s_host && s_host->WriteIopMemory(address, data, size);
	}

	u64 GetCurrentCycle()
	{
		return s_host ? s_host->CurrentCycle() : 0;
	}

	void QueuePendingDbufQuadWrite(u32 offset_high, u32 offset_low, u32 payload)
	{
		if (s_host)
			s_host->QueueRemoteAsyncWriteQuad(offset_high, offset_low, payload);
	}

	void QueuePendingDbufBlockWrite(u32 offset_high, u32 offset_low, const u32* payload, u32 payload_quads)
	{
		if (s_host)
			s_host->QueueRemoteAsyncWriteBlock(offset_high, offset_low, payload, payload_quads);
	}

	void QueuePendingDbufByteWrite(u32 offset_high, u32 offset_low, const u8* payload, u32 byte_count)
	{
		if (s_host)
			s_host->QueueRemoteAsyncWriteBytes(offset_high, offset_low, payload, byte_count);
	}

	void FlushPendingDbufR0RxPacket()
	{
		if (s_host)
			s_host->FlushPendingRemoteWrites();
	}

	u8 CalculateAcioChecksum(const u8* data, u32 byte_count)
	{
		u8 checksum = 0;
		for (u32 i = 1; i < byte_count; i++)
			checksum = static_cast<u8>(checksum + data[i]);
		return checksum;
	}

	std::array<u8, 28> BuildExternalIoProductResponse(u8 device_id)
	{
		// Dogstation's EE parser treats bytes 15..26 as a flags byte plus the B22C.zin version stamp.
		std::array<u8, 28> response = {
			0xaa, 0xaa, device_id, 0x02, 0x00, 0x00,
			0xaa, 0xa5, device_id, 0x02, 0x05,
			'C', 'R', '-', '2', 0x36, 0x01, 0x00, 0x07, 'B', '2', '2', 'C', 0x00, 0x00, 0x02, 0x01,
			0x00,
		};
		response[5] = CalculateAcioChecksum(response.data(), 5);
		response[27] = CalculateAcioChecksum(response.data() + 6, 21);
		return response;
	}

	std::array<u8, 13> BuildExternalIoCommandAck(u8 device_id, u8 command)
	{
		return {
			0xaa, 0xaa, device_id, command, 0x00, static_cast<u8>(0xaa + device_id + command),
			0xaa, 0xa5, device_id, command, 0x01, 0x00, static_cast<u8>(0xa6 + device_id + command),
		};
	}

	std::vector<u8> EscapeAcioPacket(const u8* data, u32 byte_count)
	{
		std::vector<u8> escaped;
		escaped.reserve(byte_count + 4);
		for (u32 i = 0; i < byte_count; i++)
		{
			if (data[i] == 0xaa || data[i] == 0xff)
			{
				escaped.push_back(0xff);
				escaped.push_back(static_cast<u8>(~data[i]));
			}
			else
			{
				escaped.push_back(data[i]);
			}
		}
		return escaped;
	}

	void QueueUartBytes(const u8* data, u32 byte_count)
	{
		if (byte_count == 0)
			return;

		if (s_uart_rx_fifo.size() + byte_count > UART_RX_QUEUE_LIMIT)
		{
			const size_t overflow = s_uart_rx_fifo.size() + byte_count - UART_RX_QUEUE_LIMIT;
			s_uart_rx_fifo.erase(s_uart_rx_fifo.begin(), s_uart_rx_fifo.begin() + std::min(overflow, s_uart_rx_fifo.size()));
		}

		s_uart_rx_fifo.insert(s_uart_rx_fifo.end(), data, data + byte_count);
	}

	void QueueAcioResponse(const u8* data, u32 byte_count)
	{
		if (byte_count == 0)
			return;

		std::vector<u8> packet(data, data + byte_count);
		packet.push_back(CalculateAcioChecksum(packet.data(), static_cast<u32>(packet.size())));
		std::vector<u8> escaped = EscapeAcioPacket(packet.data(), static_cast<u32>(packet.size()));
		QueueUartBytes(escaped.data(), static_cast<u32>(escaped.size()));
	}

	void QueueExternalIoWrappedResponse(const std::vector<u8>& request, const u8* response, u32 response_bytes)
	{
		std::vector<u8> packet(request);
		packet.push_back(0xaa);
		packet.push_back(IsPython1DogstationMode() ? 0x01 : 0x00);
		packet.push_back(request[2]);
		packet.push_back(request[3]);
		packet.insert(packet.end(), response, response + response_bytes);
		packet.push_back(CalculateAcioChecksum(packet.data() + request.size(), response_bytes + 4));
		QueueUartBytes(packet.data(), static_cast<u32>(packet.size()));
	}

	Python1KeyboardKey GetPython1KeyboardKeyForHostKey(u32 host_key_code)
	{
		for (const Python1KeyboardKeyMapping& mapping : PYTHON1_KEYBOARD_MAPPINGS)
		{
			const std::optional<u32> mapped_host_code = InputManager::ConvertHostKeyboardStringToCode(mapping.host_name);
			if (mapped_host_code.has_value() && mapped_host_code.value() == host_key_code)
				return {mapping.ps2_set2_make, mapping.extended};
		}

		return {};
	}

	void PushKeyboardMakeEvent(std::vector<u8>& events, Python1KeyboardKey key)
	{
		if (key.extended)
			events.push_back(0xe0);
		events.push_back(key.ps2_set2_make);
	}

	void PushKeyboardBreakEvent(std::vector<u8>& events, Python1KeyboardKey key)
	{
		if (key.extended)
			events.push_back(0xe0);
		events.push_back(0xf0);
		events.push_back(key.ps2_set2_make);
	}

	std::array<u8, 9> BuildExternalIoStatusPayload(u8 device_id, u8 command)
	{
		std::array<u8, 9> status = {0x04};
		if (IsPython1DogstationMode() && device_id == 0x01 && command == 0x26 && s_device)
			(void)s_device->PopKeyboard1Events(status.data() + 1, 8);
		return status;
	}

	Python1IOMode GetPython1IOMode()
	{
		const std::string mode = Host::GetStringSettingValue(PYTHON1_GAME_CONFIG_SECTION, "IOMode", PYTHON1_IO_MODE_JVS);
		if (StringUtil::Strcasecmp(mode.c_str(), PYTHON1_IO_MODE_EXTIO) == 0)
			return Python1IOMode::EXTIO;
		if (StringUtil::Strcasecmp(mode.c_str(), PYTHON1_IO_MODE_POPN) == 0)
			return Python1IOMode::POPN;
		if (StringUtil::Strcasecmp(mode.c_str(), PYTHON1_IO_MODE_DOGSTATION) == 0)
			return Python1IOMode::DOGSTATION;
		return Python1IOMode::JVS;
	}

	bool IsPython1DogstationMode()
	{
		return GetPython1IOMode() == Python1IOMode::DOGSTATION;
	}

	bool IsPython1ExtioMode()
	{
		return GetPython1IOMode() == Python1IOMode::EXTIO;
	}

	u32 FindChecksummedUartPacketSize(const std::vector<u8>& bytes)
	{
		for (u32 packet_size = 6; packet_size <= bytes.size(); packet_size++)
		{
			if (CalculateAcioChecksum(bytes.data(), packet_size - 1) == bytes[packet_size - 1])
				return packet_size;
		}

		return 0;
	}

	void HandleAcioUartWrite(const u32* payload, u32 payload_quads, u32 byte_count, bool extio_mode)
	{
		std::vector<u8> bytes;
		bytes.resize(byte_count);
		const u32 inline_bytes = payload_quads > 2 ? (payload_quads - 2) * sizeof(u32) : 0;
		if (byte_count > inline_bytes)
		{
			const u32 source = payload_quads > 2 ? payload[2] : 0;
			if (source == 0 || !ReadIopMemory(source, bytes.data(), byte_count))
			{
				DevCon.WriteLn("FW HLE: failed UART write source read src=0x%x bytes=0x%x", source, byte_count);
				return;
			}
		}
		else
		{
			for (u32 offset = 0; offset < byte_count; offset++)
			{
				const u32 word = payload[2 + offset / sizeof(u32)];
				bytes[offset] = static_cast<u8>(word >> (24 - ((offset & 3) * 8)));
			}
		}

		if (bytes.size() >= 4 && bytes[0] == 0xaa && bytes[1] == 0xaa && bytes[2] == 0xaa && bytes[3] == 0x55)
		{
			QueueUartBytes(KONAMI_EXTERNAL_IO_SYNC_RESPONSE, sizeof(KONAMI_EXTERNAL_IO_SYNC_RESPONSE));
		}
		else if (bytes.size() >= 4 && bytes[0] == 0xaa && bytes[1] == 0xaa && bytes[2] == 0x00 && bytes[3] == 0x00)
		{
			QueueUartBytes(KONAMI_EXTERNAL_IO_RESET_RESPONSE, sizeof(KONAMI_EXTERNAL_IO_RESET_RESPONSE));
		}
		else if (bytes.size() >= 4 && bytes[0] == 0xaa && bytes[1] == 0xaa && bytes[2] == 0x00 && bytes[3] == 0x01)
		{
			if (IsPython1DogstationMode())
				QueueUartBytes(KONAMI_DOGSTATION_EXTERNAL_IO_COUNT_RESPONSE, sizeof(KONAMI_DOGSTATION_EXTERNAL_IO_COUNT_RESPONSE));
			else
				QueueUartBytes(KONAMI_EXTERNAL_IO_COUNT_RESPONSE, sizeof(KONAMI_EXTERNAL_IO_COUNT_RESPONSE));
		}
		else if (IsPython1DogstationMode() && bytes.size() >= 6 && bytes[0] == 0xaa && bytes[1] == 0xaa && bytes[2] >= 0x01 && bytes[2] <= 0x02 && bytes[3] == 0x02)
		{
			const auto response = BuildExternalIoProductResponse(bytes[2]);
			QueueUartBytes(response.data(), static_cast<u32>(response.size()));
		}
		else if (IsPython1DogstationMode() && bytes.size() >= 6 && bytes[0] == 0xaa && bytes[1] == 0xaa && bytes[2] >= 0x01 && bytes[2] <= 0x02 && bytes[3] == 0x03)
		{
			const auto response = BuildExternalIoCommandAck(bytes[2], bytes[3]);
			QueueUartBytes(response.data(), static_cast<u32>(response.size()));
		}
		else if (IsPython1DogstationMode() && bytes.size() >= 6 && bytes[0] == 0xaa && bytes[1] == 0xaa && bytes[2] >= 0x01 && bytes[2] <= 0x02 && bytes[3] == 0x04)
		{
			const auto response = BuildExternalIoCommandAck(bytes[2], bytes[3]);
			QueueUartBytes(response.data(), static_cast<u32>(response.size()));
		}
		else if (!IsPython1DogstationMode() && bytes.size() >= 6 && bytes[0] == 0xaa && bytes[1] == 0xaa && bytes[2] == 0x01 && bytes[3] == 0x02)
		{
			QueueUartBytes(KONAMI_EXTERNAL_IO_PRODUCT_RESPONSE, sizeof(KONAMI_EXTERNAL_IO_PRODUCT_RESPONSE));
		}
		else if (!IsPython1DogstationMode() && bytes.size() >= 6 && bytes[0] == 0xaa && bytes[1] == 0xaa && bytes[2] == 0x01 && bytes[3] == 0x03)
		{
			QueueUartBytes(KONAMI_EXTERNAL_IO_STARTUP_RESPONSE, sizeof(KONAMI_EXTERNAL_IO_STARTUP_RESPONSE));
		}
		else if (extio_mode && bytes.size() >= 5 && bytes[0] == 0xaa && bytes[1] == 0x01 && bytes[2] == 0x00 && bytes[3] == 0x02)
		{
			QueueAcioResponse(KONAMI_ICCA_PRODUCT_RESPONSE, sizeof(KONAMI_ICCA_PRODUCT_RESPONSE));
		}
		else if (extio_mode && bytes.size() >= 5 && bytes[0] == 0xaa && bytes[1] == 0x01 && bytes[2] == 0x00 && bytes[3] == 0x03)
		{
			QueueAcioResponse(KONAMI_ICCA_STARTUP_RESPONSE, sizeof(KONAMI_ICCA_STARTUP_RESPONSE));
		}
		else if (extio_mode && bytes.size() >= 5 && bytes[0] == 0xaa && bytes[1] == 0x01 &&
			bytes[2] == 0x01 && (bytes[3] == 0x31 || bytes[3] == 0x34 || bytes[3] == 0x35))
		{
			QueueAcioResponse(KONAMI_ICCA_STATUS_RESPONSE, sizeof(KONAMI_ICCA_STATUS_RESPONSE));
		}
		else if (extio_mode && bytes.size() >= 4 && ((bytes[0] + bytes[1] + bytes[2]) & 0x7f) == bytes[3])
		{
			// DDR foot-panel external I/O packets are raw 4-byte writes, not ACIO-framed.
			constexpr u8 DDR_EXTERNAL_IO_ACK[] = {0x11};
			QueueUartBytes(DDR_EXTERNAL_IO_ACK, sizeof(DDR_EXTERNAL_IO_ACK));
		}
		else if (bytes.size() >= 6 && bytes[0] == 0xaa && bytes[1] == 0x00)
		{
			const u32 packet_size = FindChecksummedUartPacketSize(bytes);
			if (packet_size == 0)
				return;

			std::vector<u8> request(bytes.begin(), bytes.begin() + packet_size);
			const bool dogstation_mode = IsPython1DogstationMode();
			const u8 command = dogstation_mode ? (request[3] & ~0x40) : request[3];
			const u8 ok_response[] = {
				static_cast<u8>(dogstation_mode ? 0x01 : 0x00),
				static_cast<u8>(dogstation_mode && request[2] != 0x01 ? 0x01 : 0x00),
			};
			switch (command)
			{
				case 0x00:
				case 0x01:
				case 0x10:
				case 0x11:
				case 0x12:
				case 0x14:
				case 0x15:
				case 0x16:
				case 0x1e:
				case 0x1f:
					QueueExternalIoWrappedResponse(request, ok_response, sizeof(ok_response));
					break;
				case 0x18:
				{
					const std::array<u8, 0x82> empty_card_data = {};
					QueueExternalIoWrappedResponse(request, empty_card_data.data(), static_cast<u32>(empty_card_data.size()));
					break;
				}
				case 0x26:
				case 0x36:
				{
					if (!dogstation_mode)
						break;
					const std::array<u8, 9> status = BuildExternalIoStatusPayload(request[2], command);
					QueueExternalIoWrappedResponse(request, status.data(), static_cast<u32>(status.size()));
					break;
				}
				default:
					break;
			}
		}
	}

	bool QueueUartReadData(u32 requested_bytes)
	{
		const u32 byte_count = std::min<u32>(requested_bytes, static_cast<u32>(s_uart_rx_fifo.size()));
		if (byte_count == 0)
			return false;

		std::vector<u32> callback(2 + ((byte_count + 1) / 2));
		callback[1] = (byte_count << 16) | s_uart_rx_fifo[0];
		for (u32 i = 1; i < byte_count; i++)
		{
			u32& word = callback[2 + ((i - 1) / 2)];
			if (((i - 1) & 1) == 0)
				word |= static_cast<u32>(s_uart_rx_fifo[i]) << 16;
			else
				word |= s_uart_rx_fifo[i];
		}

		for (u32& word : callback)
			word = ByteSwap32(word);
		QueuePendingDbufBlockWrite(0xfffe, KONAMI_UART_STATUS_OFFSET, callback.data(), static_cast<u32>(callback.size()));
		s_uart_rx_fifo.erase(s_uart_rx_fifo.begin(), s_uart_rx_fifo.begin() + byte_count);
		return true;
	}

	std::string GetPython1GamePath(const char* key, const char* env_key)
	{
		const char* env_path = std::getenv(env_key);
		return (env_path && env_path[0]) ? std::string(env_path) : Host::GetStringSettingValue(PYTHON1_GAME_CONFIG_SECTION, key, "");
	}

	std::string GetHddImagePath()
	{
		return GetPython1GamePath("HddImageFile", "PCSX2_FW_HDD_IMAGE_FILE");
	}

	std::string GetBbsramPath()
	{
		return GetPython1GamePath("BBSRamFile", "PCSX2_FW_BBSRAM_FILE");
	}

	std::string GetBootromPath()
	{
		return GetPython1GamePath("IOBootRomFile", "PCSX2_FW_BOOTROM_FILE");
	}

	std::string GetConfigRomPath()
	{
		return GetPython1GamePath("IOConfigRomFile", "PCSX2_FW_CROM_FILE");
	}

	std::string GetDallasDonglePath(u32 slot)
	{
		return slot == 0 ?
			GetPython1GamePath("DongleBlackFile", "PCSX2_FW_DONGLE_BLACK_FILE") :
			GetPython1GamePath("DongleWhiteFile", "PCSX2_FW_DONGLE_WHITE_FILE");
	}

	u8 CalculateDallasCrc8(const u8* data, u32 size, u8 crc)
	{
		for (u32 index = 0; index < size; index++)
		{
			u8 in_byte = data[index];
			for (u32 bit = 0; bit < 8; bit++)
			{
				const u8 mix = (crc ^ in_byte) & 1;
				crc >>= 1;
				if (mix != 0)
					crc ^= 0x8c;
				in_byte >>= 1;
			}
		}
		return crc;
	}

	void SetDallasKeyResponseFromId(const u8* id)
	{
		s_dallas_dongle_slots[0][0] = 0x14;
		std::copy_n(id, 6, s_dallas_dongle_slots[0].data() + 1);
		s_dallas_dongle_slots[0][7] = CalculateDallasCrc8(s_dallas_dongle_slots[0].data(), DALLAS_DONGLE_SERIAL_SIZE - 1, 0);
	}

	bool IsValidUnicastMac(const u8* mac)
	{
		bool all_zero = true;
		bool all_ff = true;
		for (u32 i = 0; i < 6; i++)
		{
			all_zero &= mac[i] == 0;
			all_ff &= mac[i] == 0xff;
		}

		return !all_zero && !all_ff && (mac[0] & 1) == 0;
	}

	bool IsBootromMacBackupRecordValid()
	{
		const u8* record = s_bootrom + KONAMI_BOOTROM_MAC_BACKUP_OFFSET;
		if (!IsValidUnicastMac(record))
			return false;

		for (u32 i = 0; i < 6; i++)
		{
			if ((record[i] ^ record[i + 6]) != 0xff)
				return false;
		}

		return true;
	}

	u8 ToFsciHexDigit(u8 value)
	{
		return value < 10 ? static_cast<u8>('0' + value) : static_cast<u8>('A' + value - 10);
	}

	void WriteFsciHexByte(u8* dest, u8 value)
	{
		dest[0] = ToFsciHexDigit(value >> 4);
		dest[1] = ToFsciHexDigit(value & 0x0f);
	}

	u8 CalculateFsciChecksum(const u8* data, u32 size)
	{
		u32 checksum = 0;
		for (u32 i = 0; i < size; i++)
			checksum += data[i];
		while (checksum > 0xff)
			checksum = (checksum & 0xff) + (checksum >> 8);
		return static_cast<u8>(checksum);
	}

	void BuildFsciMacFrame(u8* dest)
	{
		dest[0] = '@';
		WriteFsciHexByte(dest + 1, 0x0d);
		dest[3] = 'M';
		for (u32 i = 0; i < s_factory_mac.size(); i++)
			WriteFsciHexByte(dest + 4 + i * 2, s_factory_mac[i]);
		WriteFsciHexByte(dest + 16, CalculateFsciChecksum(dest + 3, 0x0d));
	}

	void UpdateFsciMacResponses()
	{
		for (u32 i = 0; i < KONAMI_FSCI_INITIAL_MAC_FRAMES; i++)
			BuildFsciMacFrame(s_fsci_mac_stream.data() + i * KONAMI_FSCI_MAC_FRAME_SIZE);
	}

	void ResetFsciStream()
	{
		s_fsci_stream_offset = 0;
		UpdateFsciMacResponses();
	}

	u32 BuildFsciReadResponse(u8* dest, u32 byte_count)
	{
		const u32 mac_stream_size = static_cast<u32>(s_fsci_mac_stream.size());
		const u32 available = s_fsci_stream_offset < mac_stream_size ? mac_stream_size - s_fsci_stream_offset : 0;
		const u32 response_bytes = std::min<u32>({byte_count, KONAMI_FSCI_MAX_READ_BYTES, available});

		if (response_bytes != 0)
		{
			std::memcpy(dest, s_fsci_mac_stream.data() + s_fsci_stream_offset, response_bytes);
			s_fsci_stream_offset += response_bytes;
		}

		return response_bytes;
	}

	void UpdateFactoryMacFromBootrom()
	{
		std::memcpy(s_factory_mac.data(), KONAMI_FACTORY_MAC, s_factory_mac.size());
		if (!IsBootromMacBackupRecordValid())
		{
			DevCon.WriteLn("FW HLE: warning: invalid BootROM MAC backup at 0x%x, using default %02x:%02x:%02x:%02x:%02x:%02x",
				KONAMI_BOOTROM_MAC_BACKUP_OFFSET,
				s_factory_mac[0], s_factory_mac[1], s_factory_mac[2], s_factory_mac[3], s_factory_mac[4], s_factory_mac[5]);
			UpdateFsciMacResponses();
			return;
		}

		std::memcpy(s_factory_mac.data(), s_bootrom + KONAMI_BOOTROM_MAC_BACKUP_OFFSET, s_factory_mac.size());
		UpdateFsciMacResponses();
	}

	u32 PackDallasSerialResponseWord(const u8* data)
	{
		// Dallas serial responses are copied out of the status buffer as native words.
		return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) |
			(static_cast<u32>(data[2]) << 16) | (static_cast<u32>(data[3]) << 24);
	}

	std::array<u32, 3> BuildDallasDongleSerialResponse(u32 slot)
	{
		const std::array<u8, DALLAS_DONGLE_TOTAL_SIZE>& dongle = s_dallas_dongle_slots[slot];
		return {{0, PackDallasSerialResponseWord(dongle.data()), PackDallasSerialResponseWord(dongle.data() + 4)}};
	}

	std::optional<u32> GetDallasDongleSlotForKey(u32 key)
	{
		if (key >= DALLAS_DONGLE_SLOT_COUNT)
			return std::nullopt;

		return key;
	}

	void UpdateDallasKeyResponseFromBootrom()
	{
		SetDallasKeyResponseFromId(KONAMI_DEFAULT_DALLAS_ID);
		for (u32 offset = 0xf000; offset <= BOOTROM_SIZE - 0x20; offset += 0x10)
		{
			const u8* record = s_bootrom + offset;
			if (record[0x10] != 'G' || record[0x1a] != 'J' || record[0x1b] != 'A')
				continue;

			SetDallasKeyResponseFromId(record);
			return;
		}
	}

	bool EnsureParentDirectoryForFile(const std::string& path)
	{
		const std::string directory(Path::GetDirectory(path));
		return directory.empty() || FileSystem::CreateDirectoryPath(directory.c_str(), false);
	}

	bool WriteBinaryFile(const std::string& path, const void* data, size_t size)
	{
		if (path.empty() || !EnsureParentDirectoryForFile(path))
			return false;

		std::FILE* file = std::fopen(path.c_str(), "wb");
		if (!file)
			return false;

		const size_t written = std::fwrite(data, 1, size, file);
		std::fclose(file);
		return written == size;
	}

	bool NormalizeDallasDongleData(const u8* raw, std::array<u8, DALLAS_DONGLE_TOTAL_SIZE>* output)
	{
		const bool mame_format =
			CalculateDallasCrc8(raw + DALLAS_DONGLE_PAYLOAD_SIZE, DALLAS_DONGLE_SERIAL_SIZE - 1, 0) == raw[DALLAS_DONGLE_TOTAL_SIZE - 1];
		if (mame_format)
		{
			std::memcpy(output->data(), raw + DALLAS_DONGLE_PAYLOAD_SIZE, DALLAS_DONGLE_SERIAL_SIZE);
			std::memcpy(output->data() + DALLAS_DONGLE_SERIAL_SIZE, raw, DALLAS_DONGLE_PAYLOAD_SIZE);
			return true;
		}

		const bool old_format =
			CalculateDallasCrc8(raw, DALLAS_DONGLE_SERIAL_SIZE - 1, 0) == raw[DALLAS_DONGLE_SERIAL_SIZE - 1];
		if (old_format)
		{
			std::memcpy(output->data(), raw, DALLAS_DONGLE_TOTAL_SIZE);
			return true;
		}

		return false;
	}

	void LoadConfigRom()
	{
		s_io_config_rom.clear();
		const std::string path = GetConfigRomPath();
		if (path.empty())
			return;

		const std::optional<std::vector<u8>> data = FileSystem::ReadBinaryFile(path.c_str());
		if (!data)
		{
			DevCon.WriteLn("FW HLE: failed to open config ROM path=%s", path.c_str());
			return;
		}

		if (data->empty() || (data->size() & 3) != 0)
		{
			DevCon.WriteLn("FW HLE: invalid config ROM path=%s bytes=0x%zx", path.c_str(), data->size());
			return;
		}

		s_io_config_rom.resize(data->size() / sizeof(u32));
		for (size_t i = 0; i < s_io_config_rom.size(); i++)
			s_io_config_rom[i] = ReadBigEndian32(data->data() + (i * sizeof(u32)));
	}

	void LoadDallasDongles()
	{
		for (u32 slot = 0; slot < DALLAS_DONGLE_SLOT_COUNT; slot++)
		{
			s_dallas_dongle_slots[slot].fill(0);
			s_dallas_dongle_loaded[slot] = false;
			s_dallas_dongle_dirty[slot] = false;

			const std::string path = GetDallasDonglePath(slot);
			if (path.empty())
				continue;

			std::FILE* file = std::fopen(path.c_str(), "rb");
			if (!file)
			{
				DevCon.WriteLn("FW HLE: failed to open Dallas dongle slot=%u path=%s", slot, path.c_str());
				continue;
			}

			u8 raw[DALLAS_DONGLE_TOTAL_SIZE] = {};
			const size_t read = std::fread(raw, 1, sizeof(raw), file);
			std::fclose(file);
			if (read != sizeof(raw) || !NormalizeDallasDongleData(raw, &s_dallas_dongle_slots[slot]))
			{
				DevCon.WriteLn("FW HLE: invalid Dallas dongle slot=%u path=%s bytes=0x%zx", slot, path.c_str(), read);
				continue;
			}

			s_dallas_dongle_loaded[slot] = true;
		}

		// Initialize internal Dallas if one wasn't loaded from file.
		if (!s_dallas_dongle_loaded[0])
		{
			UpdateDallasKeyResponseFromBootrom();
			s_dallas_dongle_loaded[0] = true;
		}
	}

	void BuildGeneratedBootrom()
	{
		std::memset(s_bootrom, 0xff, sizeof(s_bootrom));
		std::memcpy(s_bootrom, KONAMI_FACTORY_MAC, sizeof(KONAMI_FACTORY_MAC));
		std::memcpy(s_bootrom + KONAMI_BOOTROM_MAC_BACKUP_OFFSET, KONAMI_MAC_BACKUP, sizeof(KONAMI_MAC_BACKUP));
		std::memcpy(s_bootrom + 0xf030, KONAMI_WE2K3_DALLAS_BOOTROM_RECORD, sizeof(KONAMI_WE2K3_DALLAS_BOOTROM_RECORD));
	}

	void LoadBootrom()
	{
		s_bootrom_dirty = false;

		BuildGeneratedBootrom();

		const std::string path = GetBootromPath();
		if (path.empty())
		{
			UpdateFactoryMacFromBootrom();
			UpdateDallasKeyResponseFromBootrom();
			return;
		}

		std::FILE* file = std::fopen(path.c_str(), "rb");
		if (!file)
		{
			UpdateFactoryMacFromBootrom();
			UpdateDallasKeyResponseFromBootrom();
			(void)WriteBinaryFile(path, s_bootrom, sizeof(s_bootrom));
			return;
		}

		(void)std::fread(s_bootrom, 1, sizeof(s_bootrom), file);
		std::fclose(file);
		UpdateFactoryMacFromBootrom();
		UpdateDallasKeyResponseFromBootrom();
	}

	void LoadBbsram()
	{
		std::memset(s_bbsram, 0, sizeof(s_bbsram));
		s_bbsram_dirty = false;

		const std::string path = GetBbsramPath();
		if (path.empty())
			return;

		std::FILE* file = std::fopen(path.c_str(), "rb");
		if (!file)
		{
			(void)WriteBinaryFile(path, s_bbsram, sizeof(s_bbsram));
			return;
		}

		(void)std::fread(s_bbsram, 1, sizeof(s_bbsram), file);
		std::fclose(file);
	}

	void SaveBootromIfDirty()
	{
		if (!s_bootrom_dirty)
			return;

		const std::string path = GetBootromPath();
		if (path.empty())
			return;

		std::FILE* file = std::fopen(path.c_str(), "wb");
		if (!file)
			return;

		const size_t written = std::fwrite(s_bootrom, 1, sizeof(s_bootrom), file);
		std::fclose(file);
		if (written == sizeof(s_bootrom))
			s_bootrom_dirty = false;
	}

	void SaveBbsramIfDirty()
	{
		if (!s_bbsram_dirty)
			return;

		const std::string path = GetBbsramPath();
		if (path.empty())
			return;

		std::FILE* file = std::fopen(path.c_str(), "wb");
		if (!file)
			return;

		const size_t written = std::fwrite(s_bbsram, 1, sizeof(s_bbsram), file);
		std::fclose(file);
		if (written == sizeof(s_bbsram))
			s_bbsram_dirty = false;
	}

	void SaveDallasDongleIfDirty()
	{
		for (u32 slot = 0; slot < DALLAS_DONGLE_SLOT_COUNT; slot++)
		{
			if (!s_dallas_dongle_dirty[slot])
				continue;

			const std::string path = GetDallasDonglePath(slot);
			if (path.empty())
			{
				DevCon.WriteLn("FW HLE: Dallas dongle slot=%u is dirty but has no associated file path", slot);
				continue;
			}

			std::FILE* file = std::fopen(path.c_str(), "wb");
			if (!file)
				return;

			const size_t written = std::fwrite(s_dallas_dongle_slots[slot].data(), 1, sizeof(s_dallas_dongle_slots[slot]), file);
			std::fclose(file);
			if (written == sizeof(s_dallas_dongle_slots[slot]))
				s_dallas_dongle_dirty[slot] = false;
		}
	}

	void CloseHddImageFile()
	{
		if (!s_hdd_image_file)
			return;

		std::fclose(s_hdd_image_file);
		s_hdd_image_file = nullptr;
		s_hdd_image_open_path.clear();
		s_hdd_image_writable = false;
	}

	std::FILE* GetHddImageFile(const std::string& path, bool writable = false)
	{
		if (s_hdd_image_file && s_hdd_image_open_path == path && (!writable || s_hdd_image_writable))
			return s_hdd_image_file;

		CloseHddImageFile();
		s_hdd_image_file = std::fopen(path.c_str(), writable ? "rb+" : "rb");
		if (s_hdd_image_file)
		{
			s_hdd_image_open_path = path;
			s_hdd_image_writable = writable;
		}
		return s_hdd_image_file;
	}

	u32 ByteSwap32(u32 value)
	{
		return (value >> 24) | ((value >> 8) & 0x0000ff00) | ((value & 0x0000ff00) << 8) | (value << 24);
	}

	u32 ReadBigEndian32(const u8* data)
	{
		return (static_cast<u32>(data[0]) << 24) | (static_cast<u32>(data[1]) << 16) |
			(static_cast<u32>(data[2]) << 8) | static_cast<u32>(data[3]);
	}

	void StopSubboardAdpcmPlayback()
	{
		s_subboard_adpcm.encoded.clear();
		s_subboard_adpcm.samples.clear();
		s_subboard_adpcm.decoder_step_index = {};
		s_subboard_adpcm.decoder_pcm_sample = {};
		s_subboard_adpcm.position = 0;
		s_subboard_adpcm.loop_start = 0;
		s_subboard_adpcm.loop_end = 0;
		s_subboard_adpcm.resample_accumulator = 0;
		s_subboard_adpcm.filter_state = {};
		s_subboard_adpcm.fade_in_remaining = 0;
		s_subboard_adpcm.fade_out_remaining = 0;
		s_subboard_adpcm.sector = 0;
		s_subboard_adpcm.playing = false;
		s_subboard_adpcm.looping = false;
		s_subboard_adpcm.stopping = false;
		s_subboard_adpcm.filter_initialized = false;
	}

	void RequestStopSubboardAdpcmPlayback()
	{
		if (!s_subboard_adpcm.playing)
		{
			StopSubboardAdpcmPlayback();
			return;
		}

		s_subboard_adpcm.stopping = true;
		s_subboard_adpcm.fade_in_remaining = 0;
		if (s_subboard_adpcm.fade_out_remaining == 0)
			s_subboard_adpcm.fade_out_remaining = KONAMI_ADPCM_FADE_SAMPLES;
	}

	void ResetSubboardAdpcmPlayback()
	{
		StopSubboardAdpcmPlayback();
		s_subboard_adpcm.volume_left = 0;
		s_subboard_adpcm.volume_right = 0;
	}

	s16 DecodeKonamiAdpcmNibble(u8 nibble, s32& step_index, s32& pcm_sample)
	{
		const s32 step = KONAMI_ADPCM_STEPS[step_index];
		s32 delta = (step >> 3) + ((step >> 2) & -(nibble & 1)) +
			((step >> 1) & -((nibble >> 1) & 1)) + (step & -((nibble >> 2) & 1));
		if (nibble & 0x08)
			delta = -delta;

		step_index = std::clamp<s32>(step_index + KONAMI_ADPCM_INDEX_CHANGES[nibble], 0, 48);
		pcm_sample = std::clamp<s32>(pcm_sample + delta, -32768, 32767);
		return static_cast<s16>(pcm_sample);
	}

	bool EnsureSubboardAdpcmDecoded(size_t position)
	{
		if (position >= s_subboard_adpcm.encoded.size())
			return false;

		while (s_subboard_adpcm.samples.size() <= position)
		{
			const u8 byte = s_subboard_adpcm.encoded[s_subboard_adpcm.samples.size()];
			s_subboard_adpcm.samples.push_back({
				DecodeKonamiAdpcmNibble(byte >> 4, s_subboard_adpcm.decoder_step_index[0], s_subboard_adpcm.decoder_pcm_sample[0]),
				DecodeKonamiAdpcmNibble(byte & 0x0f, s_subboard_adpcm.decoder_step_index[1], s_subboard_adpcm.decoder_pcm_sample[1]),
			});
		}

		return true;
	}

	std::array<s16, 2> GetSubboardAdpcmSampleAt(size_t position)
	{
		return EnsureSubboardAdpcmDecoded(position) ? s_subboard_adpcm.samples[position] : std::array<s16, 2>{};
	}

	bool StartSubboardAdpcmPlayback(u32 sector)
	{
		const std::string path = GetHddImagePath();
		if (path.empty())
		{
			DevCon.WriteLn("FW HLE: no Python 1 HDD image configured for ADPCM sector=0x%x", sector);
			StopSubboardAdpcmPlayback();
			return false;
		}

		std::FILE* file = GetHddImageFile(path);
		if (!file)
		{
			DevCon.WriteLn("FW HLE: failed to open %s for ADPCM sector=0x%x", path.c_str(), sector);
			StopSubboardAdpcmPlayback();
			return false;
		}

		const u64 offset = static_cast<u64>(sector) * SECTOR_SIZE;
		if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0)
		{
			DevCon.WriteLn("FW HLE: failed to seek %s to ADPCM sector=0x%x", path.c_str(), sector);
			StopSubboardAdpcmPlayback();
			return false;
		}

		std::array<u8, KONAMI_ADPCM_HEADER_SIZE> header;
		if (std::fread(header.data(), 1, header.size(), file) != header.size())
		{
			DevCon.WriteLn("FW HLE: short ADPCM header read sector=0x%x", sector);
			StopSubboardAdpcmPlayback();
			return false;
		}

		if (header[0] != 'A' || header[1] != 'D' || header[2] != 'P')
		{
			DevCon.WriteLn("FW HLE: ADPCM sector=0x%x missing ADP header magic", sector);
			StopSubboardAdpcmPlayback();
			return false;
		}

		const u32 encoded_size = ReadBigEndian32(header.data() + 4);
		if (encoded_size == 0 || encoded_size > KONAMI_ADPCM_MAX_ENCODED_BYTES)
		{
			DevCon.WriteLn("FW HLE: ADPCM sector=0x%x invalid encoded size=0x%x", sector, encoded_size);
			StopSubboardAdpcmPlayback();
			return false;
		}

		std::vector<u8> encoded(encoded_size);
		const size_t read = std::fread(encoded.data(), 1, encoded.size(), file);
		if (read != encoded.size())
		{
			DevCon.WriteLn("FW HLE: short ADPCM read sector=0x%x requested=0x%zx read=0x%zx", sector, encoded.size(), read);
			StopSubboardAdpcmPlayback();
			return false;
		}

		bool looping = false;
		size_t loop_start = 0;
		size_t loop_end = encoded.size();
		if (header[3] == 0x02)
		{
			const u32 loop_start_file = ReadBigEndian32(header.data() + 8);
			const u32 loop_end_file = ReadBigEndian32(header.data() + 12);
			const u32 loop_start_byte = loop_start_file > KONAMI_ADPCM_HEADER_SIZE ? loop_start_file - KONAMI_ADPCM_HEADER_SIZE : 0;
			const u32 loop_end_byte = loop_end_file > KONAMI_ADPCM_HEADER_SIZE ? std::min(loop_end_file - KONAMI_ADPCM_HEADER_SIZE, encoded_size) : encoded_size;
			loop_start = std::min<size_t>(loop_start_byte, encoded.size());
			loop_end = std::min<size_t>(loop_end_byte, encoded.size());
			looping = loop_start < loop_end;
		}

		s_subboard_adpcm.encoded = std::move(encoded);
		s_subboard_adpcm.samples.clear();
		s_subboard_adpcm.samples.reserve(s_subboard_adpcm.encoded.size());
		s_subboard_adpcm.decoder_step_index = {};
		s_subboard_adpcm.decoder_pcm_sample = {};
		s_subboard_adpcm.position = 0;
		s_subboard_adpcm.loop_start = loop_start;
		s_subboard_adpcm.loop_end = loop_end;
		s_subboard_adpcm.resample_accumulator = 0;
		s_subboard_adpcm.filter_state = {};
		s_subboard_adpcm.fade_in_remaining = KONAMI_ADPCM_FADE_SAMPLES;
		s_subboard_adpcm.fade_out_remaining = 0;
		s_subboard_adpcm.sector = sector;
		s_subboard_adpcm.playing = !s_subboard_adpcm.encoded.empty();
		s_subboard_adpcm.looping = looping;
		s_subboard_adpcm.stopping = false;
		s_subboard_adpcm.filter_initialized = false;

		return s_subboard_adpcm.playing;
	}

	void AdvanceSubboardAdpcmPlayback()
	{
		if (!s_subboard_adpcm.playing)
			return;

		s_subboard_adpcm.position++;
		const size_t end_position = s_subboard_adpcm.looping ? s_subboard_adpcm.loop_end : s_subboard_adpcm.encoded.size();
		if (s_subboard_adpcm.position < end_position)
			return;

		if (s_subboard_adpcm.looping && s_subboard_adpcm.loop_start < s_subboard_adpcm.loop_end)
		{
			s_subboard_adpcm.position = s_subboard_adpcm.loop_start;
			return;
		}

		StopSubboardAdpcmPlayback();
	}

	std::array<s16, 2> GetNextSubboardAdpcmSample()
	{
		const size_t end_position = s_subboard_adpcm.looping ? s_subboard_adpcm.loop_end : s_subboard_adpcm.encoded.size();
		const size_t next_position = s_subboard_adpcm.position + 1;
		if (next_position < end_position)
			return GetSubboardAdpcmSampleAt(next_position);
		if (s_subboard_adpcm.looping && s_subboard_adpcm.loop_start < s_subboard_adpcm.loop_end)
			return GetSubboardAdpcmSampleAt(s_subboard_adpcm.loop_start);
		return GetSubboardAdpcmSampleAt(s_subboard_adpcm.position);
	}

	std::array<float, 2> GetInterpolatedSubboardAdpcmSample()
	{
		const std::array<s16, 2> current = GetSubboardAdpcmSampleAt(s_subboard_adpcm.position);
		const std::array<s16, 2> next = GetNextSubboardAdpcmSample();
		const float fraction = static_cast<float>(s_subboard_adpcm.resample_accumulator) /
			static_cast<float>(KONAMI_ADPCM_OUTPUT_SAMPLE_RATE);
		return {
			static_cast<float>(current[0]) + ((static_cast<float>(next[0]) - static_cast<float>(current[0])) * fraction),
			static_cast<float>(current[1]) + ((static_cast<float>(next[1]) - static_cast<float>(current[1])) * fraction),
		};
	}

	std::array<float, 2> FilterSubboardAdpcmSample(const std::array<float, 2>& sample)
	{
		if (!s_subboard_adpcm.filter_initialized)
		{
			s_subboard_adpcm.filter_state = sample;
			s_subboard_adpcm.filter_initialized = true;
			return sample;
		}

		for (u32 channel = 0; channel < 2; channel++)
			s_subboard_adpcm.filter_state[channel] += (sample[channel] - s_subboard_adpcm.filter_state[channel]) * KONAMI_ADPCM_LOWPASS_ALPHA;
		return s_subboard_adpcm.filter_state;
	}

	float GetSubboardAdpcmEnvelope(bool* stop_after_mix)
	{
		float envelope = 1.0f;
		if (s_subboard_adpcm.fade_in_remaining != 0)
		{
			envelope *= static_cast<float>(KONAMI_ADPCM_FADE_SAMPLES - s_subboard_adpcm.fade_in_remaining) /
				static_cast<float>(KONAMI_ADPCM_FADE_SAMPLES);
			s_subboard_adpcm.fade_in_remaining--;
		}

		if (s_subboard_adpcm.stopping)
		{
			envelope *= static_cast<float>(s_subboard_adpcm.fade_out_remaining) /
				static_cast<float>(KONAMI_ADPCM_FADE_SAMPLES);
			if (s_subboard_adpcm.fade_out_remaining != 0)
				s_subboard_adpcm.fade_out_remaining--;
			if (s_subboard_adpcm.fade_out_remaining == 0 && stop_after_mix)
				*stop_after_mix = true;
		}

		return envelope;
	}

	s32 ScaleSubboardAdpcmSample(float sample, u32 volume, float envelope)
	{
		const float clamped_volume = static_cast<float>(std::min<u32>(volume, 0xffff));
		return static_cast<s32>((sample * clamped_volume * envelope) / 65536.0f);
	}

	void MixSubboardAdpcmAudio(s32* left, s32* right)
	{
		if (!s_subboard_adpcm.playing || !left || !right || s_subboard_adpcm.position >= s_subboard_adpcm.encoded.size())
			return;
		if (!EnsureSubboardAdpcmDecoded(s_subboard_adpcm.position))
		{
			StopSubboardAdpcmPlayback();
			return;
		}

		bool stop_after_mix = false;
		const float envelope = GetSubboardAdpcmEnvelope(&stop_after_mix);
		const std::array<float, 2> sample = FilterSubboardAdpcmSample(GetInterpolatedSubboardAdpcmSample());
		*left += ScaleSubboardAdpcmSample(sample[0], s_subboard_adpcm.volume_left, envelope);
		*right += ScaleSubboardAdpcmSample(sample[1], s_subboard_adpcm.volume_right, envelope);

		s_subboard_adpcm.resample_accumulator += KONAMI_ADPCM_INPUT_SAMPLE_RATE;
		while (s_subboard_adpcm.playing && s_subboard_adpcm.resample_accumulator >= KONAMI_ADPCM_OUTPUT_SAMPLE_RATE)
		{
			s_subboard_adpcm.resample_accumulator -= KONAMI_ADPCM_OUTPUT_SAMPLE_RATE;
			AdvanceSubboardAdpcmPlayback();
		}

		if (stop_after_mix)
			StopSubboardAdpcmPlayback();
	}

	bool TryReadKonamiConfigRom(u64 offset, u32* value)
	{
		if ((offset & ~0x3ffull) != FireWire::CROM_BASE)
			return false;

		const u32 relative_offset = offset - FireWire::CROM_BASE;
		if ((relative_offset & 3) != 0)
			return false;

		const u32 index = relative_offset >> 2;
		if (!s_io_config_rom.empty())
		{
			if (index >= s_io_config_rom.size())
				return false;

			*value = s_io_config_rom[index];
			return true;
		}

		if (index >= sizeof(KONAMI_IO_BOARD_CROM) / sizeof(KONAMI_IO_BOARD_CROM[0]))
			return false;

		*value = KONAMI_IO_BOARD_CROM[index];
		return true;
	}

	bool TryReadKonamiQuadlet(u64 offset, u32* value)
	{
		if (TryReadKonamiConfigRom(offset, value))
			return true;

		if (offset == KONAMI_BOOT_READY_OFFSET)
		{
			// Captured response to 0xfffd:0x05735734 immediately before the first CF command.
			*value = 0x01000000;
			return true;
		}

		if (offset == KONAMI_RUNTIME_READY_OFFSET)
		{
			// Captured runtime response to 0xfffd:0x05735730 before command traffic resumes.
			*value = 0;
			return true;
		}

		return false;
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

	u64 CalculateStorageReadCycles(size_t byte_count)
	{
		const u64 base_cycles = std::max<u64>(1, PSXCLK / 2000); // 0.5 ms command/DMA setup.
		const u64 transfer_cycles = (static_cast<u64>(byte_count) * PSXCLK + KONAMI_STORAGE_READ_BYTES_PER_SECOND - 1) /
			KONAMI_STORAGE_READ_BYTES_PER_SECOND;
		return base_cycles + std::max<u64>(1, transfer_cycles);
	}

	void SchedulePendingSectorStatusEvent()
	{
		if (!s_host)
			return;

		if (s_pending_sector_status_writes.empty())
		{
			s_host->ClearEvent();
			return;
		}

		const u64 ready_cycle = s_pending_sector_status_writes.front().ready_cycle;
		const u64 current_cycle = GetCurrentCycle();
		const u64 delta = ready_cycle > current_cycle ? ready_cycle - current_cycle : 1;
		s_host->ScheduleEvent(delta);
	}

	void ServicePendingSectorStatusWrites()
	{
		bool queued_status = false;
		const u64 current_cycle = GetCurrentCycle();
		while (!s_pending_sector_status_writes.empty() && s_pending_sector_status_writes.front().ready_cycle <= current_cycle)
		{
			const PendingSectorStatusWrite status = s_pending_sector_status_writes.front();
			s_pending_sector_status_writes.erase(s_pending_sector_status_writes.begin());
			QueuePendingDbufQuadWrite(0xfffe, status.status_offset, ByteSwap32(status.checksum));
			queued_status = true;
		}

		if (queued_status)
			FlushPendingDbufR0RxPacket();
		SchedulePendingSectorStatusEvent();
	}

	bool PerformFireWireIopDmaWrite(u32 dest, const u8* data, size_t size)
	{
		// HLE the receive-side DMA effect of an async FireWire block write into IOP RAM.
		if (!WriteIopMemory(dest, data, static_cast<u32>(size)))
		{
			DevCon.WriteLn("FW HLE: failed FireWire IOP DMA write of 0x%zx bytes to IOP 0x%x", size, dest);
			return false;
		}

		if (FW_VERBOSE_LOGS)
			DevCon.WriteLn("FW HLE: FireWire IOP DMA off_hi=0x1000 off_low=0x%x bytes=0x%zx", dest, size);
		return true;
	}

	bool ShouldDeferSectorReadStatus(u32 status_offset)
	{
		return status_offset == KONAMI_CF_STATUS_OFFSET || status_offset == KONAMI_ATA_STATUS_OFFSET;
	}

	void QueueSectorReadStatus(u32 status_offset, u32 checksum, size_t byte_count, bool defer_status)
	{
		if (!defer_status)
		{
			QueuePendingDbufQuadWrite(0xfffe, status_offset, ByteSwap32(checksum));
			return;
		}

		const u64 start_cycle = std::max(s_next_sector_read_ready_cycle, GetCurrentCycle());
		const u64 ready_cycle = start_cycle + CalculateStorageReadCycles(byte_count);
		s_next_sector_read_ready_cycle = ready_cycle;
		s_pending_sector_status_writes.push_back({ready_cycle, status_offset, checksum});
		SchedulePendingSectorStatusEvent();
	}

	bool QueuePendingSectorAndStatusPackets(u32 dest, const std::vector<u8>& data, u32 status_offset, bool defer_status)
	{
		if (!PerformFireWireIopDmaWrite(dest, data.data(), data.size()))
			return false;

		// Sector payload is already in IOP RAM; only the status write should be visible to the FW stack.
		QueueSectorReadStatus(status_offset, CalculateReadStatusChecksum(data), data.size(), defer_status);
		return true;
	}

	bool HleReadSectors(u32 sector, u32 count, u32 dest, u32 status_offset)
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

		const std::string path = GetHddImagePath();
		if (path.empty())
		{
			DevCon.WriteLn("FW HLE: no Python 1 HDD image configured");
			return false;
		}

		std::vector<u8> data(static_cast<size_t>(bytes64));
		std::FILE* file = GetHddImageFile(path);
		if (!file)
		{
			DevCon.WriteLn("FW HLE: failed to open %s", path.c_str());
			return false;
		}

		if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0)
		{
			DevCon.WriteLn("FW HLE: failed to seek %s to 0x%llx", path.c_str(), static_cast<unsigned long long>(offset));
			return false;
		}

		const size_t bytes = static_cast<size_t>(bytes64);
		const size_t read = std::fread(data.data(), 1, bytes, file);
		if (read != bytes)
		{
			DevCon.WriteLn("FW HLE: short read %s offset=0x%llx requested=0x%zx read=0x%zx", path.c_str(), static_cast<unsigned long long>(offset), bytes, read);
			return false;
		}

		return QueuePendingSectorAndStatusPackets(dest, data, status_offset, ShouldDeferSectorReadStatus(status_offset));
	}

	bool HleWriteSectors(u32 sector, u32 count, u32 source, u32 status_offset)
	{
		if (count == 0)
		{
			QueuePendingDbufQuadWrite(0xfffe, status_offset, 0);
			return true;
		}

		const u64 offset = static_cast<u64>(sector) * SECTOR_SIZE;
		const u64 bytes64 = static_cast<u64>(count) * SECTOR_SIZE;
		if (bytes64 > 16 * 1024 * 1024)
		{
			DevCon.WriteLn("FW HLE: refusing oversized sector write sector=0x%x count=0x%x source=0x%x", sector, count, source);
			return false;
		}

		const std::string path = GetHddImagePath();
		if (path.empty())
		{
			DevCon.WriteLn("FW HLE: no Python 1 HDD image configured");
			return false;
		}

		std::vector<u8> data(static_cast<size_t>(bytes64));
		if (!ReadIopMemory(source, data.data(), static_cast<u32>(data.size())))
		{
			DevCon.WriteLn("FW HLE: failed sector write DMA read src=0x%x bytes=0x%zx", source, data.size());
			return false;
		}

		std::FILE* file = GetHddImageFile(path, true);
		if (!file)
		{
			DevCon.WriteLn("FW HLE: failed to open %s writable", path.c_str());
			return false;
		}

		if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0)
		{
			DevCon.WriteLn("FW HLE: failed to seek %s to 0x%llx for write", path.c_str(), static_cast<unsigned long long>(offset));
			return false;
		}

		const size_t bytes = static_cast<size_t>(bytes64);
		const size_t written = std::fwrite(data.data(), 1, bytes, file);
		if (written != bytes || std::fflush(file) != 0)
		{
			DevCon.WriteLn("FW HLE: short write %s offset=0x%llx requested=0x%zx wrote=0x%zx", path.c_str(), static_cast<unsigned long long>(offset), bytes, written);
			return false;
		}

		QueuePendingDbufQuadWrite(0xfffe, status_offset, 0);
		return true;
	}

	bool HleBbsramCommand(const u32* payload, u32 payload_quads)
	{
		if (payload_quads < 4)
			return false;

		const u32 subop = payload[0];
		const u32 offset = payload[1];
		const u32 byte_count = payload[2];
		const u32 dest = payload[3];
		if (offset > BBSRAM_SIZE || byte_count > BBSRAM_SIZE - offset)
		{
			DevCon.WriteLn("FW HLE: BBSRAM request out of range subop=0x%x offset=0x%x bytes=0x%x dest=0x%x", subop, offset, byte_count, dest);
			return false;
		}

		if (subop == 0)
		{
			if (byte_count != 0 && dest != 0)
			{
				for (u32 transfer_offset = 0; transfer_offset < byte_count; transfer_offset += KONAMI_DBUF_WRITEB_MAX_PAYLOAD)
				{
					const u32 chunk = std::min<u32>(KONAMI_DBUF_WRITEB_MAX_PAYLOAD, byte_count - transfer_offset);
					QueuePendingDbufByteWrite(0x1000, dest + transfer_offset, s_bbsram + offset + transfer_offset, chunk);
				}
			}
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_BBSRAM_STATUS_OFFSET, 0);
			return true;
		}

		if (subop == 1)
		{
			if (byte_count != 0)
			{
				bool copied = false;
				if (dest != 0)
				{
					copied = ReadIopMemory(dest, s_bbsram + offset, byte_count);
					if (!copied)
						DevCon.WriteLn("FW HLE: failed BBSRAM write DMA read src=0x%x bytes=0x%x", dest, byte_count);
				}

				if (!copied)
				{
					const u32 inline_start = dest != 0 ? 3 : 4;
					const u32 inline_bytes = payload_quads > inline_start ? (payload_quads - inline_start) * sizeof(u32) : 0;
					if (byte_count > inline_bytes)
						return false;

					for (u32 i = 0; i < byte_count; i++)
					{
						const u32 word = payload[inline_start + (i / sizeof(u32))];
						s_bbsram[offset + i] = static_cast<u8>(word >> (24 - ((i & 3) * 8)));
					}
				}

				const bool volatile_test_write = (offset == 0 && byte_count == KONAMI_BBSRAM_VOLATILE_TEST_BYTES);
				if (!volatile_test_write)
				{
					s_bbsram_dirty = true;
					SaveBbsramIfDirty();
				}
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

		if (subop == 0)
		{
			const std::optional<u32> slot = GetDallasDongleSlotForKey(key);
			if (slot.has_value() && s_dallas_dongle_loaded[*slot])
			{
				std::array<u32, 3> response = BuildDallasDongleSerialResponse(*slot);
				QueuePendingDbufBlockWrite(0xfffe, KONAMI_DALLAS_STATUS_OFFSET, response.data(), static_cast<u32>(response.size()));
				return true;
			}

			QueuePendingDbufBlockWrite(0xfffe, KONAMI_DALLAS_STATUS_OFFSET,
				KONAMI_DALLAS_NO_KEY_RESPONSE,
				static_cast<u32>(sizeof(KONAMI_DALLAS_NO_KEY_RESPONSE) / sizeof(KONAMI_DALLAS_NO_KEY_RESPONSE[0])));
			return true;
		}

		if (subop == 1)
		{
			const std::optional<u32> slot = GetDallasDongleSlotForKey(key);

			if (!slot.has_value() || !s_dallas_dongle_loaded[*slot] ||
				offset > DALLAS_DONGLE_PAYLOAD_SIZE || byte_count > DALLAS_DONGLE_PAYLOAD_SIZE - offset)
			{
				QueuePendingDbufQuadWrite(0xfffe, KONAMI_DALLAS_STATUS_OFFSET, KONAMI_DALLAS_NO_KEY_RESPONSE[0]);
				return true;
			}

			if (byte_count != 0 && dest != 0)
				QueuePendingDbufByteWrite(0x1000, dest, s_dallas_dongle_slots[*slot].data() + DALLAS_DONGLE_SERIAL_SIZE + offset, byte_count);
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_DALLAS_STATUS_OFFSET, 0);
			return true;
		}

		if (subop == 2)
		{
			const std::optional<u32> slot = GetDallasDongleSlotForKey(key);
			if (!slot.has_value() || !s_dallas_dongle_loaded[*slot] ||
				offset > DALLAS_DONGLE_PAYLOAD_SIZE || byte_count > DALLAS_DONGLE_PAYLOAD_SIZE - offset)
			{
				QueuePendingDbufQuadWrite(0xfffe, KONAMI_DALLAS_STATUS_OFFSET, KONAMI_DALLAS_NO_KEY_RESPONSE[0]);
				return true;
			}

			if (byte_count != 0 && dest != 0 &&
				!ReadIopMemory(dest, s_dallas_dongle_slots[*slot].data() + DALLAS_DONGLE_SERIAL_SIZE + offset, byte_count))
			{
				DevCon.WriteLn("FW HLE: failed Dallas dongle write DMA read src=0x%x bytes=0x%x", dest, byte_count);
				QueuePendingDbufQuadWrite(0xfffe, KONAMI_DALLAS_STATUS_OFFSET, KONAMI_DALLAS_NO_KEY_RESPONSE[0]);
				return true;
			}
			s_dallas_dongle_dirty[*slot] = true;
			SaveDallasDongleIfDirty();
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_DALLAS_STATUS_OFFSET, 0);
			return true;
		}

		QueuePendingDbufQuadWrite(0xfffe, KONAMI_DALLAS_STATUS_OFFSET, KONAMI_DALLAS_NO_KEY_RESPONSE[0]);
		return true;
	}

	bool HleFsciCommand(const u32* payload, u32 payload_quads)
	{
		const u32 subop = payload[0];
		const u32 byte_count = payload_quads > 1 ? payload[1] : 0;
		const u32 dest = payload_quads > 2 ? payload[2] : 0;
		if (subop == 0)
		{
			ResetFsciStream();
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_FSCI_STATUS_OFFSET, 0);
			return true;
		}

		if (subop == 2 && byte_count != 0 && dest != 0)
		{
			// Pop'n treats a parsed FSCI MAC as an FC/card probe success and skips the
			// backup-RAM initialization path that loads bbsram0:/settings.
			if (GetPython1IOMode() == Python1IOMode::POPN)
			{
				QueuePendingDbufQuadWrite(0xfffe, KONAMI_FSCI_STATUS_OFFSET, 0);
				return true;
			}

			std::array<u8, KONAMI_FSCI_MAX_READ_BYTES> response;
			const u32 response_bytes = BuildFsciReadResponse(response.data(), byte_count);
			if (!WriteIopMemory(dest, response.data(), response_bytes))
			{
				DevCon.WriteLn("FW HLE: failed FSCI DMA write dest=0x%x bytes=0x%x", dest, response_bytes);
				QueuePendingDbufQuadWrite(0xfffe, KONAMI_FSCI_STATUS_OFFSET, 0);
				return true;
			}
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
		const u32 status = ByteSwap32(2);

		const bool extio_mode = IsPython1ExtioMode();
		bool queued_read_data = false;
		if (subop == 1 && word1 != 0)
			HandleAcioUartWrite(payload, payload_quads, word1, extio_mode);
		else if (subop == 2 && (extio_mode || !s_uart_rx_fifo.empty()))
			queued_read_data = QueueUartReadData(UART_CALLBACK_BYTE_LIMIT);

		QueuePendingDbufQuadWrite(0xfffe, KONAMI_UART_STATUS_OFFSET, status);
		if ((extio_mode && subop == 2) || queued_read_data)
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_UART_STATUS_OFFSET, ByteSwap32(1));
		return true;
	}

	bool HleAdpcmCommand(const u32* payload, u32 payload_quads)
	{
		const u32 command_words = payload_quads > 0 ? std::min<u32>(payload[0] + 1, payload_quads - 1) : 0;
		bool has_sector_high = false;
		bool has_sector_low = false;
		u32 sector_high = 0;
		u32 sector_low = 0;
		for (u32 i = 0; i < command_words; i++)
		{
			const u32 word = payload[i + 1];
			const u32 command = word >> 24;
			switch (command)
			{
				case 0x00:
					if (word == 0)
						RequestStopSubboardAdpcmPlayback();
					break;
				case 0x02:
					sector_high = word & 0xffff;
					has_sector_high = true;
					break;
				case 0x03:
					sector_low = word & 0xffff;
					has_sector_low = true;
					break;
				case 0x04:
					break;
				case 0x05:
					s_subboard_adpcm.volume_left = word & 0xffff;
					break;
				case 0x06:
					s_subboard_adpcm.volume_right = word & 0xffff;
					break;
				default:
					break;
			}
		}

		if (has_sector_high && has_sector_low)
			StartSubboardAdpcmPlayback((sector_high << 16) | sector_low);

		QueuePendingDbufQuadWrite(0xfffe, KONAMI_ADPCM_STATUS_OFFSET, 0);
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
		if (offset > BOOTROM_SIZE || byte_count > BOOTROM_SIZE - offset)
		{
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_BOOTROM_STATUS_OFFSET, 0x41000000);
			return true;
		}

		if (subop == 0)
		{
			if (byte_count != 0 && dest != 0)
			{
				for (u32 transfer_offset = 0; transfer_offset < byte_count; transfer_offset += KONAMI_DBUF_WRITEB_MAX_PAYLOAD)
				{
					const u32 chunk = std::min<u32>(KONAMI_DBUF_WRITEB_MAX_PAYLOAD, byte_count - transfer_offset);
					QueuePendingDbufByteWrite(0x1000, dest + transfer_offset, s_bootrom + offset + transfer_offset, chunk);
				}
			}
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_BOOTROM_STATUS_OFFSET, 0x41000000);
			return true;
		}

		if (subop == 1)
		{
			if (byte_count != 0 && dest != 0)
			{
				if (ReadIopMemory(dest, s_bootrom + offset, byte_count))
				{
					s_bootrom_dirty = true;
					SaveBootromIfDirty();
				}
				else
				{
					DevCon.WriteLn("FW HLE: failed BOOTROM write DMA read src=0x%x bytes=0x%x", dest, byte_count);
				}
			}
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_BOOTROM_STATUS_OFFSET, 0x41000000);
			return true;
		}

		QueuePendingDbufQuadWrite(0xfffe, KONAMI_BOOTROM_STATUS_OFFSET, 0x41000000);
		return true;
	}

	void BuildOfflineConfiguredIp()
	{
		u32 network_id = s_bbsram[KONAMI_BBSRAM_NETWORK_ID_OFFSET];
		if (network_id < 1 || network_id > 6)
			network_id = 1;

		s_net_property_response[0] = 192;
		s_net_property_response[1] = 168;
		s_net_property_response[2] = 1;
		s_net_property_response[3] = static_cast<u8>(10 + network_id);
	}

	bool TryGetNetProperty(u32 property, u32 requested_bytes, const u8** response, u32* response_bytes)
	{
		const u8* data = nullptr;
		u32 data_bytes = 0;

		switch (property)
		{
			case 0x101:
				if (KONAMI_NET_FORCE_OFFLINE)
				{
					BuildOfflineConfiguredIp();
					data = s_net_property_response.data();
					data_bytes = sizeof(KONAMI_NET_OFFLINE_IP_ADDRESS);
				}
				else
				{
					data = KONAMI_NET_IP_ADDRESS;
					data_bytes = sizeof(KONAMI_NET_IP_ADDRESS);
				}
				break;
			case 1:
				data = KONAMI_NET_SUBNET_MASK;
				data_bytes = sizeof(KONAMI_NET_SUBNET_MASK);
				break;
			case 3:
				data = KONAMI_NET_FORCE_OFFLINE ? KONAMI_NET_OFFLINE_GATEWAY : KONAMI_NET_GATEWAY;
				data_bytes = KONAMI_NET_FORCE_OFFLINE ? sizeof(KONAMI_NET_OFFLINE_GATEWAY) : sizeof(KONAMI_NET_GATEWAY);
				break;
			case 6:
				data = KONAMI_NET_FORCE_OFFLINE ? KONAMI_NET_OFFLINE_DNS : KONAMI_NET_PRIMARY_DNS;
				data_bytes = KONAMI_NET_FORCE_OFFLINE ? sizeof(KONAMI_NET_OFFLINE_DNS) : sizeof(KONAMI_NET_PRIMARY_DNS);
				break;
			case 0xf:
				data = KONAMI_NET_MACHINE_ID;
				data_bytes = sizeof(KONAMI_NET_MACHINE_ID);
				break;
			case 0x2a:
				data = s_factory_mac.data();
				data_bytes = static_cast<u32>(s_factory_mac.size());
				break;
			case 0x36:
				data = KONAMI_NET_SECONDARY_DNS;
				data_bytes = sizeof(KONAMI_NET_SECONDARY_DNS);
				break;
			default:
				return false;
		}

		*response_bytes = std::min(requested_bytes, data_bytes);
		*response = data;
		return true;
	}

	u16 ReadBe16(const u8* data)
	{
		return static_cast<u16>((static_cast<u16>(data[0]) << 8) | data[1]);
	}

	void WriteBe16(std::vector<u8>& data, u16 value)
	{
		data.push_back(static_cast<u8>(value >> 8));
		data.push_back(static_cast<u8>(value));
	}

	void WriteBe32(std::vector<u8>& data, u32 value)
	{
		data.push_back(static_cast<u8>(value >> 24));
		data.push_back(static_cast<u8>(value >> 16));
		data.push_back(static_cast<u8>(value >> 8));
		data.push_back(static_cast<u8>(value));
	}

	u32 ReadIopNative32(u32 address)
	{
		u32 value = 0;
		if (!ReadIopMemory(address, &value, sizeof(value)))
			return 0;
		return value;
	}

	u16 OnesComplementSum(const u8* data, u32 byte_count, u32 sum = 0)
	{
		for (u32 offset = 0; offset < byte_count; offset += 2)
		{
			u16 word = static_cast<u16>(data[offset]) << 8;
			if (offset + 1 < byte_count)
				word |= data[offset + 1];
			sum += word;
			while (sum >> 16)
				sum = (sum & 0xffff) + (sum >> 16);
		}

		return static_cast<u16>(~sum);
	}

	bool BuildDnsPayloadResponse(const u8* dns, u32 dns_bytes, std::vector<u8>* response)
	{
		if (dns_bytes < 12 || ReadBe16(dns + 4) == 0)
			return false;

		u32 question_end = 12;
		while (question_end < dns_bytes && dns[question_end] != 0)
		{
			const u8 label_bytes = dns[question_end];
			if ((label_bytes & 0xc0) != 0 || label_bytes == 0 || question_end + 1 + label_bytes >= dns_bytes)
				return false;
			question_end += 1 + label_bytes;
		}
		if (question_end + 5 > dns_bytes)
			return false;
		question_end += 5;

		response->clear();
		response->reserve(question_end + 16);
		response->insert(response->end(), dns, dns + question_end);
		(*response)[2] = 0x81;
		(*response)[3] = 0x80;
		(*response)[6] = 0x00;
		(*response)[7] = 0x01;
		(*response)[8] = 0x00;
		(*response)[9] = 0x00;
		(*response)[10] = 0x00;
		(*response)[11] = 0x00;
		WriteBe16(*response, 0xc00c);
		WriteBe16(*response, 1);
		WriteBe16(*response, 1);
		WriteBe32(*response, 60);
		WriteBe16(*response, 4);
		WriteBe32(*response, KONAMI_NET_GATEWAY[0] << 24 | KONAMI_NET_GATEWAY[1] << 16 | KONAMI_NET_GATEWAY[2] << 8 | KONAMI_NET_GATEWAY[3]);
		return true;
	}

	bool BuildDnsResponsePacket(const u8* request, u32 request_bytes, u32 src_ip, u32 dst_ip, std::vector<u8>* response)
	{
		if (request_bytes < 28)
			return false;

		const u8 ip_header_bytes = static_cast<u8>((request[0] & 0x0f) * 4);
		if ((request[0] >> 4) != 4 || ip_header_bytes < 20 || request_bytes < ip_header_bytes + 8)
			return false;
		if (request[9] != 17)
			return false;

		const u8* udp = request + ip_header_bytes;
		const u16 src_port = ReadBe16(udp);
		const u16 dst_port = ReadBe16(udp + 2);
		const u16 udp_bytes = ReadBe16(udp + 4);
		if (dst_port != 53 || udp_bytes < 8 || ip_header_bytes + udp_bytes > request_bytes)
			return false;

		std::vector<u8> dns_response;
		if (!BuildDnsPayloadResponse(udp + 8, udp_bytes - 8, &dns_response))
			return false;

		const u16 reply_udp_bytes = static_cast<u16>(8 + dns_response.size());
		const u16 reply_ip_bytes = static_cast<u16>(20 + reply_udp_bytes);

		response->clear();
		response->reserve(reply_ip_bytes);
		response->push_back(0x45);
		response->push_back(0x00);
		WriteBe16(*response, reply_ip_bytes);
		WriteBe16(*response, ReadBe16(request + 4));
		WriteBe16(*response, 0x4000);
		response->push_back(64);
		response->push_back(17);
		WriteBe16(*response, 0);
		WriteBe32(*response, src_ip);
		WriteBe32(*response, dst_ip);
		const u16 ip_checksum = OnesComplementSum(response->data(), 20);
		(*response)[10] = static_cast<u8>(ip_checksum >> 8);
		(*response)[11] = static_cast<u8>(ip_checksum);

		WriteBe16(*response, dst_port);
		WriteBe16(*response, src_port);
		WriteBe16(*response, reply_udp_bytes);
		WriteBe16(*response, 0);
		response->insert(response->end(), dns_response.begin(), dns_response.end());

		std::vector<u8> pseudo_header;
		pseudo_header.reserve(12 + reply_udp_bytes);
		WriteBe32(pseudo_header, src_ip);
		WriteBe32(pseudo_header, dst_ip);
		pseudo_header.push_back(0);
		pseudo_header.push_back(17);
		WriteBe16(pseudo_header, reply_udp_bytes);
		pseudo_header.insert(pseudo_header.end(), response->begin() + 20, response->end());
		const u16 udp_checksum = OnesComplementSum(pseudo_header.data(), static_cast<u32>(pseudo_header.size()));
		(*response)[26] = static_cast<u8>(udp_checksum >> 8);
		(*response)[27] = static_cast<u8>(udp_checksum);
		return true;
	}

	void MaybeQueueNetReply(u32 channel, const u32* payload, u32 payload_quads)
	{
		if (KONAMI_NET_FORCE_OFFLINE)
			return;

		if (channel >= KONAMI_NET_CHANNEL_COUNT || payload_quads < 4 || payload[3] == 0)
			return;

		const u32 data_address = payload[2];
		const u32 byte_count = payload[3];
		if (byte_count > 0x2000)
			return;

		std::vector<u8> request(byte_count);
		if (!ReadIopMemory(data_address, request.data(), byte_count))
			return;

		std::vector<u8> response;
		if (BuildDnsResponsePacket(request.data(), byte_count, 0xc0a83201, 0xc0a83202, &response) ||
			(payload_quads >= 5 && (payload[4] & 0xffff) == 53 && BuildDnsPayloadResponse(request.data(), byte_count, &response)))
		{
			PendingNetPacket packet;
			packet.data = std::move(response);
			packet.source_ip = 0x0132a8c0;
			packet.source_port = 53;
			s_net_rx_packets[channel].push_back(std::move(packet));
		}
	}

	bool HleNetCommand(u32 command_offset, const u32* payload, u32 payload_quads)
	{
		if (command_offset < KONAMI_NET_COMMAND_OFFSET_BASE)
			return false;

		const u32 relative_offset = command_offset - KONAMI_NET_COMMAND_OFFSET_BASE;
		if ((relative_offset % KONAMI_NET_COMMAND_STRIDE) != 0)
			return false;

		const u32 channel = relative_offset / KONAMI_NET_COMMAND_STRIDE;
		if (channel >= KONAMI_NET_CHANNEL_COUNT || payload_quads == 0)
			return false;

		const u32 command = payload[0];
		switch (command)
		{
			case 0:
			case 1:
			case 2:
			case 3:
			case 5:
			case 6:
			case 7:
			case 0x0b:
			case 0x0c:
			case 0x13:
			case 0x14:
			case 0x15:
			case 0x16:
			case 0x17:
			case 0x1d:
			case 0x1e:
				break;
			default:
				return false;
		}

		// SUBBOARD's NET RPC path sends setup commands, then waits until NETWriteCallback
		// writes a status block at 0xb0000 + channel * 0x1000 and sets its event flag.
		const u8* property_response = nullptr;
		u32 property_response_bytes = 0;
		if (command == 0x1e)
		{
			if (payload_quads < 4 || !TryGetNetProperty(payload[1], payload[3], &property_response, &property_response_bytes))
				return false;

			QueuePendingDbufByteWrite(0x1000, payload[2], property_response, property_response_bytes);
		}
		else if ((command == 0x14 || command == 0x15) && payload_quads < 4)
		{
			return false;
		}
		else if (command == 0x15 || command == 0x17)
		{
			MaybeQueueNetReply(channel, payload, payload_quads);
		}

		PendingNetPacket received_packet;
		bool has_received_packet = false;
		if ((command == 0x16 || command == 0x17) && payload_quads >= 4 && !s_net_rx_packets[channel].empty())
		{
			received_packet = std::move(s_net_rx_packets[channel].front());
			s_net_rx_packets[channel].erase(s_net_rx_packets[channel].begin());
			const u32 max_bytes = std::min<u32>(payload[3], 0x2000);
			if (received_packet.data.size() > max_bytes)
				received_packet.data.resize(max_bytes);
			if (!received_packet.data.empty())
				QueuePendingDbufByteWrite(0x1000, payload[2], received_packet.data.data(), static_cast<u32>(received_packet.data.size()));
			has_received_packet = true;
		}

		std::array<u32, 8> response = {};
		if (command == 7)
			response[1] = 1;
		else if (command == 0x13 || command == 0x14 || command == 0x15)
			response[1] = ByteSwap32(payload[3]);
		else if ((command == 0x16 || command == 0x17) && has_received_packet)
		{
			response[1] = ByteSwap32(static_cast<u32>(received_packet.data.size()));
			response[2] = received_packet.source_ip;
			response[3] = (ReadIopNative32(payload[5]) & 0xffff0000) | received_packet.source_port;
		}
		else if (command == 0x1e)
			response[1] = ByteSwap32(property_response_bytes);
		QueuePendingDbufBlockWrite(0xfffe, KONAMI_NET_RESPONSE_OFFSET_BASE + channel * KONAMI_NET_RESPONSE_STRIDE,
			response.data(), static_cast<u32>(response.size()));
		return true;
	}

	u32 GetP1IOJammaStatus()
	{
		u32 status = JAMMA_STATUS_NEUTRAL;
		for (const P1IOJammaMapping& mapping : P1IO_JAMMA_MAPPINGS)
		{
			if (s_device && s_device->GetBindingValue(mapping.bind) >= 0.5f)
				status &= ~(mapping.active_low_mask & JAMMA_ACTIVE_LOW_INPUT_MASK);
		}

		return status;
	}

	u32 GetP1IOBindState()
	{
		return s_device ? s_device->GetBindingState() : 0;
	}

	bool UpdateP1IOCoinCounters(u32 bind_state)
	{
		const bool bind_state_changed = bind_state != s_last_p1io_bind_state;
		const u32 pressed = bind_state & ~s_last_p1io_bind_state;
		if (pressed & (1u << P1IO_BIND_COIN1))
			s_p1io_coin_counters[0]++;
		if (pressed & (1u << P1IO_BIND_COIN2))
			s_p1io_coin_counters[1]++;
		s_last_p1io_bind_state = bind_state;
		return bind_state_changed;
	}

	void AddP1IOInputSourceBit(u32& source_bits, u32 bind_state, P1IOBinding binding, u32 source_bit)
	{
		if (bind_state & (1u << binding))
			source_bits |= source_bit;
	}

	u32 BuildP1IOJVSInputSourceBits(u32 bind_state)
	{
		u32 source_bits = 0;
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_TEST, P1IO_SOURCE_TEST);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_SERVICE, P1IO_SOURCE_SERVICE);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_START, P1IO_SOURCE_P1_START);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_UP, P1IO_SOURCE_P1_UP);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_DOWN, P1IO_SOURCE_P1_DOWN);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_LEFT, P1IO_SOURCE_P1_LEFT);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_RIGHT, P1IO_SOURCE_P1_RIGHT);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_BUTTON1, P1IO_SOURCE_P1_BUTTON1);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_BUTTON2, P1IO_SOURCE_P1_BUTTON2);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_BUTTON3, P1IO_SOURCE_P1_BUTTON3);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_BUTTON4, P1IO_SOURCE_P1_BUTTON4);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_BUTTON5, P1IO_SOURCE_P1_BUTTON5);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_BUTTON6, P1IO_SOURCE_P1_BUTTON6);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_START, P1IO_SOURCE_P2_START);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_UP, P1IO_SOURCE_P2_UP);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_DOWN, P1IO_SOURCE_P2_DOWN);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_LEFT, P1IO_SOURCE_P2_LEFT);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_RIGHT, P1IO_SOURCE_P2_RIGHT);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_BUTTON1, P1IO_SOURCE_P2_BUTTON1);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_BUTTON2, P1IO_SOURCE_P2_BUTTON2);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_BUTTON3, P1IO_SOURCE_P2_BUTTON3);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_BUTTON4, P1IO_SOURCE_P2_BUTTON4);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_BUTTON5, P1IO_SOURCE_P2_BUTTON5);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_BUTTON6, P1IO_SOURCE_P2_BUTTON6);
		return source_bits;
	}

	u32 BuildP1IODDRInputSourceBits(u32 bind_state)
	{
		u32 source_bits = 0;
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_TEST, P1IO_SOURCE_TEST);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_SERVICE, P1IO_SOURCE_SERVICE);

		// DSF uses the normal left/right source bits for the foot panel, while
		// the 1P selector uses the adjacent button-2/button-3 source bits.
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_START, P1IO_SOURCE_P1_START);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_UP, P1IO_SOURCE_P1_UP);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_DOWN, P1IO_SOURCE_P1_DOWN);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_LEFT, P1IO_SOURCE_P1_LEFT);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_RIGHT, P1IO_SOURCE_P1_RIGHT);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_BUTTON1, P1IO_SOURCE_P1_BUTTON2);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_BUTTON2, P1IO_SOURCE_P1_BUTTON3);

		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_START, P1IO_SOURCE_P2_START);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_UP, P1IO_SOURCE_P2_UP);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_DOWN, P1IO_SOURCE_P2_DOWN);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_LEFT, P1IO_SOURCE_P2_LEFT);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_RIGHT, P1IO_SOURCE_P2_RIGHT);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_BUTTON1, P1IO_SOURCE_P2_BUTTON2);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_BUTTON2, P1IO_SOURCE_P2_BUTTON3);
		return source_bits;
	}

	u32 BuildP1IOPopnInputSourceBits(u32 bind_state)
	{
		u32 source_bits = 0;
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_TEST, P1IO_SOURCE_TEST);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_SERVICE, P1IO_SOURCE_SERVICE);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_START, P1IO_SOURCE_P1_START);

		// Pop'n 9 reads SW1-SW9 from these JAMMA source bits:
		// white/yellow/green/blue/red/blue/green/yellow/white, left to right.
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_UP, P1IO_SOURCE_P1_UP);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_DOWN, P1IO_SOURCE_P1_DOWN);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_LEFT, P1IO_SOURCE_P1_LEFT);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_RIGHT, P1IO_SOURCE_P1_RIGHT);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P1_BUTTON1, P1IO_SOURCE_P1_BUTTON1);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_UP, P1IO_SOURCE_P2_UP);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_DOWN, P1IO_SOURCE_P2_DOWN);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_LEFT, P1IO_SOURCE_P2_LEFT);
		AddP1IOInputSourceBit(source_bits, bind_state, P1IO_BIND_P2_RIGHT, P1IO_SOURCE_P2_RIGHT);
		return source_bits;
	}

	u32 BuildP1IOInputSourceBits(u32 bind_state, Python1IOMode io_mode)
	{
		if (io_mode == Python1IOMode::POPN)
			return BuildP1IOPopnInputSourceBits(bind_state);
		if (io_mode == Python1IOMode::EXTIO)
			return BuildP1IODDRInputSourceBits(bind_state);
		return BuildP1IOJVSInputSourceBits(bind_state);
	}

	std::array<u32, JAMMA_INPUT_REPORT_QUADS> BuildP1IOJammaAttachReport(u32 jamma_status)
	{
		return {{
			0x98062caa, 0x00000000, 0x00000000, 0x00000000,
			JAMMA_P1_JVS_PRESENT, 0x80000000, 0x00000000, 0x01020101,
			jamma_status,
		}};
	}

	std::array<u32, JAMMA_INPUT_REPORT_QUADS> BuildP1IOJammaLiveReport(
		u32 source_bits, u32 jamma_status, bool include_jvs_present)
	{
		return {{
			ByteSwap32(s_p1io_coin_counters[0]), ByteSwap32(s_p1io_coin_counters[1]),
			0x00000000, 0x00000000,
			ByteSwap32(source_bits) | (include_jvs_present ? JAMMA_P1_JVS_PRESENT : 0),
			0x80000000, 0x00000000, 0x01020101,
			jamma_status,
		}};
	}

	bool WriteP1IOJammaInputReport(u32 dest, u32 source_bits, u32 jamma_status, bool include_jvs_present)
	{
		const std::array<u32, JAMMA_INPUT_REPORT_QUADS> report =
			BuildP1IOJammaLiveReport(source_bits, jamma_status, include_jvs_present);
		const u32 byte_count = static_cast<u32>(report.size() * sizeof(report[0]));
		return WriteIopMemory(dest, report.data(), byte_count);
	}

	bool TryHleKonamiCommand(u64 offset, const u32* payload, u32 payload_quads)
	{
		const u32 command_offset = offset & 0xfff;
		if (FW_VERBOSE_LOGS)
			DevCon.WriteLn("FW HLE: TryHleKonamiCommand offset=0x%llx command_offset=0x%x payload_quads=0x%x", offset, command_offset, payload_quads);
		if (command_offset != KONAMI_CF_COMMAND_OFFSET && command_offset != KONAMI_ATA_COMMAND_OFFSET && payload_quads >= 1)
		{
			if (HleNetCommand(command_offset, payload, payload_quads))
				return true;

			if (command_offset == 0x0d0 && HleAdpcmCommand(payload, payload_quads))
				return true;

			if (command_offset == 0x100 && HleUartCommand(payload, payload_quads))
				return true;

			if (command_offset == 0x120 && HleDallasCommand(payload, payload_quads))
				return true;

			if (command_offset == 0x140 && HleBbsramCommand(payload, payload_quads))
				return true;

			if (command_offset == KONAMI_JAMMA_INIT_COMMAND_OFFSET && payload_quads >= 8)
			{
				const u32 bind_state = GetP1IOBindState();
				(void)UpdateP1IOCoinCounters(bind_state);
				const u32 jamma_status = GetP1IOJammaStatus();
				s_jamma_input_dest = payload[0];
				s_last_jamma_input_status = jamma_status;
				const std::array<u32, JAMMA_INPUT_REPORT_QUADS> jamma_attached_input = BuildP1IOJammaAttachReport(jamma_status);
				const u32 JAMMA_INIT_RESPONSE[] = {
					0xfe4b109c, 0x00000000, 0x00000000, 0x00000000,
					JAMMA_P1_JVS_PRESENT,
					0x81000100, 0x00000000, 0x01020101,
					jamma_status,
				};
				QueuePendingDbufBlockWrite(0x1000, payload[0], JAMMA_INIT_RESPONSE,
					sizeof(JAMMA_INIT_RESPONSE) / sizeof(JAMMA_INIT_RESPONSE[0]));
				for (u32 i = 0; i < 8; i++)
					QueuePendingDbufBlockWrite(0x1000, payload[0], jamma_attached_input.data(),
						static_cast<u32>(jamma_attached_input.size()));
				return true;
			}

			if (command_offset == KONAMI_JAMMA_OUTPUT_COMMAND_OFFSET && payload_quads >= 8)
			{
				if (IsPython1DogstationMode())
				{
					const u32 previous_latch = s_p1io_output_latch_byte;
					s_p1io_output_latch_byte = payload[4] & 0xff;
					if ((previous_latch & 0x05) == 0x05 && (s_p1io_output_latch_byte & 0x05) == 0x04)
						s_p1io_memcard_slot ^= 1;
				}

				if (s_jamma_input_dest == INVALID_JAMMA_INPUT_DEST)
				{
					return true;
				}

				const u32 bind_state = GetP1IOBindState();
				(void)UpdateP1IOCoinCounters(bind_state);
				const Python1IOMode io_mode = GetPython1IOMode();
				const u32 source_bits = BuildP1IOInputSourceBits(bind_state, io_mode);
				const u32 jamma_status = GetP1IOJammaStatus();
				if (WriteP1IOJammaInputReport(
						s_jamma_input_dest, source_bits, jamma_status, io_mode != Python1IOMode::POPN))
					s_last_jamma_input_status = jamma_status;
				return true;
			}

			if (command_offset == 0x150 && HleBootromCommand(payload, payload_quads))
				return true;

			if (command_offset == 0x160 && payload_quads >= 1)
				return HleFsciCommand(payload, payload_quads);
		}

		if ((command_offset != KONAMI_CF_COMMAND_OFFSET && command_offset != KONAMI_ATA_COMMAND_OFFSET) || payload_quads < 4)
			return false;

		const u32 subop = payload[0];
		const u32 sector = command_offset == KONAMI_ATA_COMMAND_OFFSET && payload_quads >= 5 ? payload[2] : payload[1];
		const u32 count = command_offset == KONAMI_ATA_COMMAND_OFFSET && payload_quads >= 5 ? payload[3] : payload[2];
		const u32 dest = command_offset == KONAMI_ATA_COMMAND_OFFSET && payload_quads >= 5 ? payload[4] : payload[3];
		const u32 ata_device = command_offset == KONAMI_ATA_COMMAND_OFFSET && payload_quads >= 5 ? payload[1] : 0;
		const u32 p4 = payload_quads > 4 ? payload[4] : 0;
		const u32 p5 = payload_quads > 5 ? payload[5] : 0;
		const u32 p6 = payload_quads > 6 ? payload[6] : 0;
		const u32 p7 = payload_quads > 7 ? payload[7] : 0;
		if (FW_VERBOSE_LOGS)
			DevCon.WriteLn("FW HLE: Konami command off=0x%x subop=0x%x w1=0x%x w2=0x%x w3=0x%x w4=0x%x w5=0x%x w6=0x%x w7=0x%x",
				command_offset, subop, sector, count, dest, p4, p5, p6, p7);

		if (command_offset == KONAMI_CF_COMMAND_OFFSET && subop == 0 && HleReadSectors(sector, count, dest, KONAMI_CF_STATUS_OFFSET))
			return true;
		if (command_offset == KONAMI_CF_COMMAND_OFFSET && subop == 4)
		{
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_CF_STATUS_OFFSET, 0);
			return true;
		}
		if (command_offset == KONAMI_ATA_COMMAND_OFFSET && subop == 0 && HleReadSectors(sector, count, dest, KONAMI_ATA_STATUS_OFFSET))
			return true;
		if (command_offset == KONAMI_ATA_COMMAND_OFFSET && subop == 2 && HleWriteSectors(sector, count, dest, KONAMI_ATA_STATUS_OFFSET))
			return true;
		if (command_offset == KONAMI_ATA_COMMAND_OFFSET && subop == 7)
		{
			if (ata_device == 0x14)
				s_pythonfs_formatted = true;

			const u32 status = (ata_device == 0x0a && !s_pythonfs_formatted) ? 0xffffffd1u : 0;
			QueuePendingDbufQuadWrite(0xfffe, KONAMI_ATA_STATUS_OFFSET, ByteSwap32(status));
			return true;
		}
		else if (FW_VERBOSE_LOGS)
		{
			DevCon.WriteLn("FW HLE: unhandled Konami command off=0x%x subop=0x%x w1=0x%x sector=0x%x count=0x%x dest=0x%x p4=0x%x p5=0x%x p6=0x%x p7=0x%x",
				command_offset, subop, ata_device, sector, count, dest, p4, p5, p6, p7);
		}

		return false;
	}

	bool KonamiPython1Device::Open(FireWire::FireWireDeviceHost& host)
	{
		s_host = &host;
		s_device = this;
		CloseHddImageFile();
		s_pending_sector_status_writes.clear();
		s_host->ClearEvent();
		s_uart_rx_fifo.clear();
		LoadBootrom();
		LoadBbsram();
		LoadDallasDongles();
		ResetFsciStream();
		for (auto& packets : s_net_rx_packets)
			packets.clear();
		ResetSubboardAdpcmPlayback();
		ResetBindingState();
		s_jamma_input_dest = INVALID_JAMMA_INPUT_DEST;
		s_last_jamma_input_status = JAMMA_STATUS_NEUTRAL;
		s_last_p1io_bind_state = 0;
		s_p1io_output_latch_byte = 0;
		s_p1io_memcard_slot = 1;
		s_p1io_coin_counters[0] = 0;
		s_p1io_coin_counters[1] = 0;
		s_pythonfs_formatted = false;
		LoadConfigRom();
		s_next_sector_read_ready_cycle = 0;
		return true;
	}

	void KonamiPython1Device::Close()
	{
		SaveBootromIfDirty();
		SaveBbsramIfDirty();
		SaveDallasDongleIfDirty();
		CloseHddImageFile();
		ResetSubboardAdpcmPlayback();
		s_pending_sector_status_writes.clear();
		if (s_host)
			s_host->ClearEvent();
		if (s_device == this)
			s_device = nullptr;
		s_host = nullptr;
	}

	bool KonamiPython1Device::ReadQuadlet(u64 offset, u32* value)
	{
		return TryReadKonamiQuadlet(offset, value);
	}

	bool KonamiPython1Device::Write(u64 offset, const u32* payload, u32 payload_quads)
	{
		return TryHleKonamiCommand(offset, payload, payload_quads);
	}

	void KonamiPython1Device::ServiceEvents()
	{
		ServicePendingSectorStatusWrites();
	}

	void KonamiPython1Device::MixAudio(s32* left, s32* right)
	{
		MixSubboardAdpcmAudio(left, right);
	}

	float KonamiPython1Device::GetBindingValue(u32 bind_index) const
	{
		if (bind_index >= P1IO_BIND_COUNT)
		{
			if (!IsPython1DogstationMode())
				return 0.0f;

			if (bind_index < P1IO_KEYBOARD_BIND_BASE)
				return 0.0f;

			const Python1KeyboardKey key = GetPython1KeyboardKeyForHostKey(bind_index - P1IO_KEYBOARD_BIND_BASE);
			return IsKeyboard1KeyPressed(key) ? 1.0f : 0.0f;
		}

		return (m_p1io_bind_state.load(std::memory_order_relaxed) & (1u << bind_index)) ? 1.0f : 0.0f;
	}

	void KonamiPython1Device::SetBindingValue(u32 bind_index, float value)
	{
		if (bind_index >= P1IO_KEYBOARD_BIND_BASE)
		{
			if (!IsPython1DogstationMode())
				return;

			const Python1KeyboardKey key = GetPython1KeyboardKeyForHostKey(bind_index - P1IO_KEYBOARD_BIND_BASE);
			if (key.ps2_set2_make == 0)
				return;

			const u32 key_index = key.Index();

			std::lock_guard lock(m_keyboard1_mutex);
			if (value >= 0.5f)
			{
				if (m_keyboard1_pressed[key_index])
					return;

				m_keyboard1_pressed[key_index] = true;
				PushKeyboardMakeEvent(m_keyboard1_events, key);
			}
			else
			{
				if (!m_keyboard1_pressed[key_index])
					return;

				m_keyboard1_pressed[key_index] = false;
				PushKeyboardBreakEvent(m_keyboard1_events, key);
			}
			return;
		}

		if (bind_index >= P1IO_BIND_COUNT)
			return;

		const u32 mask = 1u << bind_index;
		if (value >= 0.5f)
			m_p1io_bind_state.fetch_or(mask, std::memory_order_relaxed);
		else
			m_p1io_bind_state.fetch_and(~mask, std::memory_order_relaxed);
	}

	void KonamiPython1Device::ResetBindingState()
	{
		m_p1io_bind_state.store(0, std::memory_order_relaxed);
		std::lock_guard lock(m_keyboard1_mutex);
		m_keyboard1_pressed.fill(false);
		m_keyboard1_events.clear();
	}

	u32 KonamiPython1Device::GetBindingState() const
	{
		return m_p1io_bind_state.load(std::memory_order_relaxed);
	}

	bool KonamiPython1Device::IsKeyboard1KeyPressed(Python1KeyboardKey key) const
	{
		if (key.ps2_set2_make == 0 || key.Index() >= m_keyboard1_pressed.size())
			return false;

		std::lock_guard lock(m_keyboard1_mutex);
		return m_keyboard1_pressed[key.Index()];
	}

	u32 KonamiPython1Device::PopKeyboard1Events(u8* dest, u32 max_events)
	{
		std::lock_guard lock(m_keyboard1_mutex);
		const u32 count = std::min<u32>(max_events, static_cast<u32>(m_keyboard1_events.size()));
		std::copy_n(m_keyboard1_events.begin(), count, dest);
		m_keyboard1_events.erase(m_keyboard1_events.begin(), m_keyboard1_events.begin() + count);
		return count;
	}
} // namespace

u32 FireWire::Devices::GetKonamiPython1P1IOLatchByte()
{
	return IsPython1DogstationMode() ? s_p1io_output_latch_byte : 0;
}

u32 FireWire::Devices::GetKonamiPython1P1IOMemcardSlot()
{
	return IsPython1DogstationMode() ? s_p1io_memcard_slot : 0;
}

bool FireWire::Devices::IsKonamiPython1DogstationMode()
{
	return IsPython1DogstationMode();
}

namespace FireWire::Devices
{
	const char* KonamiPython1DeviceProxy::Name() const
	{
		return "Konami Python 1 IO Board";
	}

	const char* KonamiPython1DeviceProxy::TypeName() const
	{
		return "KonamiPython1";
	}

	const char* KonamiPython1DeviceProxy::IconName() const
	{
		return "keyboardmania-line";
	}

	std::unique_ptr<FireWireDevice> KonamiPython1DeviceProxy::CreateDevice() const
	{
		return std::make_unique<KonamiPython1Device>();
	}

	std::span<const InputBindingInfo> KonamiPython1DeviceProxy::Bindings() const
	{
		return P1IO_BINDINGS;
	}

	std::string KonamiPython1DeviceProxy::BindingConfigKey(std::string_view bind_name) const
	{
		std::string key(P1IO_CONFIG_PREFIX);
		key.append(bind_name);
		return key;
	}

	bool KonamiPython1DeviceProxy::MapAutomaticBindings(SettingsInterface& si, const std::vector<std::pair<GenericInputBinding, std::string>>& mapping) const
	{
		u32 num_mappings = 0;
		for (const InputBindingInfo& bi : P1IO_BINDINGS)
		{
			if (bi.generic_mapping == GenericInputBinding::Unknown)
				continue;

			const auto found = std::find_if(mapping.begin(), mapping.end(), [generic = bi.generic_mapping](const auto& entry) {
				return entry.first == generic;
			});
			const std::string key = BindingConfigKey(bi.name);
			if (found != mapping.end())
			{
				si.SetStringValue(FireWire::GetConfigSection(), key.c_str(), found->second.c_str());
				num_mappings++;
			}
			else
			{
				si.DeleteValue(FireWire::GetConfigSection(), key.c_str());
			}
		}

		return num_mappings > 0;
	}

	void KonamiPython1DeviceProxy::ClearBindings(SettingsInterface& si) const
	{
		for (const InputBindingInfo& bi : P1IO_BINDINGS)
			si.DeleteValue(FireWire::GetConfigSection(), BindingConfigKey(bi.name).c_str());
	}

	void KonamiPython1DeviceProxy::CopyConfiguration(SettingsInterface* dest_si, const SettingsInterface& src_si, bool copy_bindings) const
	{
		if (!copy_bindings)
			return;

		for (const InputBindingInfo& bi : P1IO_BINDINGS)
			dest_si->CopyStringValue(src_si, FireWire::GetConfigSection(), BindingConfigKey(bi.name).c_str());
	}

	float KonamiPython1DeviceProxy::GetBindingValue(const FireWireDevice* dev, u32 bind_index) const
	{
		const KonamiPython1Device* p1io = static_cast<const KonamiPython1Device*>(dev);
		return p1io ? p1io->GetBindingValue(bind_index) : 0.0f;
	}

	void KonamiPython1DeviceProxy::SetBindingValue(FireWireDevice* dev, u32 bind_index, float value) const
	{
		KonamiPython1Device* p1io = static_cast<KonamiPython1Device*>(dev);
		if (p1io)
			p1io->SetBindingValue(bind_index, value);
	}

	void KonamiPython1DeviceProxy::ResetBindingState(FireWireDevice* dev) const
	{
		KonamiPython1Device* p1io = static_cast<KonamiPython1Device*>(dev);
		if (p1io)
			p1io->ResetBindingState();
	}
} // namespace FireWire::Devices
