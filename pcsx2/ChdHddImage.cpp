// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ChdHddImage.h"

#include "common/Console.h"
#include "common/MD5Digest.h"
#include "common/Path.h"
#include "common/StringUtil.h"

#include "Config.h"

#include "libchdr/chd.h"

#include "fmt/format.h"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#include "common/RedtapeWindows.h"
#include <io.h>
#include <winioctl.h>
#elif defined(__POSIX__)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static constexpr char OVERLAY_MAP_MAGIC[8] = {'P', 'C', 'S', 'X', '2', 'H', 'O', 'V'};
static constexpr u32 OVERLAY_MAP_VERSION = 2;
static constexpr u32 OVERLAY_BLOCK_ABSENT = UINT32_MAX;

ChdHddImage::ChdHddImage() = default;

ChdHddImage::~ChdHddImage()
{
	Close();
}

bool ChdHddImage::Open(const std::string& path)
{
	Close();

	if (!OpenChd(path))
	{
		Close();
		return false;
	}

	if (!OpenOverlay(path))
	{
		Close();
		return false;
	}

	return true;
}

void ChdHddImage::Close()
{
	m_overlay_map_file.reset();
	m_overlay_file.reset();
	m_overlay_map.clear();
	m_overlay_block_buffer.clear();
	m_overlay_block_count = 0;
	m_overlay_written_block_count = 0;
	m_overlay_path.clear();
	m_overlay_map_path.clear();

	for (HunkCacheEntry& entry : m_hunk_cache)
	{
		entry.hunk = UINT32_MAX;
		entry.last_used = 0;
		entry.data.clear();
	}
	m_hunk_cache_counter = 0;

	if (m_chd)
	{
		chd_close(m_chd);
		m_chd = nullptr;
	}

	m_chd_file_handle.reset();
	m_logical_size = 0;
	m_hunk_size = 0;
	m_hunk_count = 0;
}

std::optional<u64> ChdHddImage::GetChdLogicalSize(const std::string& path)
{
	FileSystem::ManagedCFilePtr fp = FileSystem::OpenManagedSharedCFile(path.c_str(), "rb", FileSystem::FileShareMode::DenyWrite);
	if (!fp)
		return std::nullopt;

	chd_header header;
	const chd_error err = chd_read_header_file(fp.get(), &header);
	if (err != CHDERR_NONE || header.logicalbytes == 0 || (header.logicalbytes % SECTOR_SIZE) != 0)
		return std::nullopt;

	return header.logicalbytes;
}

bool ChdHddImage::IsChdFileName(const std::string& path)
{
	return StringUtil::EndsWithNoCase(Path::GetExtension(path), "chd");
}

std::optional<u64> ChdHddImage::GetHddImageLogicalSize(const std::string& path)
{
	if (IsChdFileName(path))
		return GetChdLogicalSize(path);

	const s64 size = FileSystem::GetPathFileSize(path.c_str());
	if (size < 0)
		return std::nullopt;

	return static_cast<u64>(size);
}

bool ChdHddImage::OpenChd(const std::string& path)
{
	m_chd_file_handle = FileSystem::OpenManagedSharedCFile(path.c_str(), "rb", FileSystem::FileShareMode::DenyWrite);
	if (!m_chd_file_handle)
	{
		Console.Error("CHD disk image: Failed to open '%s'", path.c_str());
		return false;
	}

	chd_error err = chd_open_file(m_chd_file_handle.get(), CHD_OPEN_READ, nullptr, &m_chd);
	if (err != CHDERR_NONE)
	{
		Console.Error("CHD disk image: Failed to open '%s': %s", path.c_str(), chd_error_string(err));
		return false;
	}

	const chd_header* header = chd_get_header(m_chd);
	if (!header || header->logicalbytes == 0 || (header->logicalbytes % SECTOR_SIZE) != 0)
	{
		Console.Error("CHD disk image: '%s' has an invalid logical size", path.c_str());
		return false;
	}

	if (header->hunkbytes == 0 || header->totalhunks == 0)
	{
		Console.Error("CHD disk image: '%s' has an invalid hunk layout", path.c_str());
		return false;
	}

	m_logical_size = header->logicalbytes;
	m_hunk_size = header->hunkbytes;
	m_hunk_count = header->totalhunks;
	DevCon.WriteLn("CHD disk image: logical size: %llu bytes, hunk size: %u bytes, hunks: %u",
		static_cast<unsigned long long>(m_logical_size), m_hunk_size, m_hunk_count);

	char metadata[256] = {};
	if (chd_get_metadata(m_chd, HARD_DISK_METADATA_TAG, 0, metadata, sizeof(metadata), nullptr, nullptr, nullptr) == CHDERR_NONE)
	{
		int cylinders = 0;
		int heads = 0;
		int sectors = 0;
		int bytes_per_sector = 0;
		if (std::sscanf(metadata, HARD_DISK_METADATA_FORMAT, &cylinders, &heads, &sectors, &bytes_per_sector) == 4 && bytes_per_sector != SECTOR_SIZE)
		{
			Console.Error("CHD disk image: '%s' uses %d-byte sectors, only 512-byte sectors are supported", path.c_str(), bytes_per_sector);
			return false;
		}
	}

	return true;
}

bool ChdHddImage::OpenOverlay(const std::string& path)
{
	m_overlay_path = GetDefaultOverlayBasePath(path);
	m_overlay_map_path = fmt::format("{}.map", m_overlay_path);
	m_overlay_block_count = (m_logical_size + OVERLAY_BLOCK_SIZE - 1) / OVERLAY_BLOCK_SIZE;

	const std::string overlay_directory(Path::GetDirectory(m_overlay_path));
	if (!FileSystem::EnsureDirectoryExists(overlay_directory.c_str(), true))
	{
		Console.Error("CHD disk image: Failed to create overlay directory '%s'", overlay_directory.c_str());
		return false;
	}

	const bool overlay_exists = FileSystem::FileExists(m_overlay_path.c_str());
	const bool map_exists = FileSystem::FileExists(m_overlay_map_path.c_str());
	if (overlay_exists != map_exists)
	{
		Console.Error("CHD disk image: overlay for '%s' is incomplete", path.c_str());
		return false;
	}

	if (!overlay_exists && !CreateOverlayFiles())
		return false;

	m_overlay_file = FileSystem::OpenManagedCFile(m_overlay_path.c_str(), "r+b");
	m_overlay_map_file = FileSystem::OpenManagedCFile(m_overlay_map_path.c_str(), "r+b");
	if (!m_overlay_file || !m_overlay_map_file)
	{
		Console.Error("CHD disk image: Failed to open overlay '%s'", m_overlay_path.c_str());
		return false;
	}

	return LoadOverlayMap();
}

bool ChdHddImage::CreateOverlayFiles()
{
	{
		FileSystem::ManagedCFilePtr overlay = FileSystem::OpenManagedCFile(m_overlay_path.c_str(), "w+b");
		if (!overlay)
		{
			Console.Error("CHD disk image: Failed to create overlay '%s'", m_overlay_path.c_str());
			return false;
		}
	}

	OverlayMapHeader header = {};
	std::memcpy(header.magic, OVERLAY_MAP_MAGIC, sizeof(header.magic));
	header.version = OVERLAY_MAP_VERSION;
	header.block_size = OVERLAY_BLOCK_SIZE;
	header.logical_size = m_logical_size;
	header.block_count = m_overlay_block_count;
	header.written_block_count = 0;

	const size_t map_entries = static_cast<size_t>(m_overlay_block_count);
	std::vector<u32> map(map_entries, OVERLAY_BLOCK_ABSENT);

	FileSystem::ManagedCFilePtr map_file = FileSystem::OpenManagedCFile(m_overlay_map_path.c_str(), "w+b");
	if (!map_file || std::fwrite(&header, sizeof(header), 1, map_file.get()) != 1 ||
		(map_entries > 0 && std::fwrite(map.data(), sizeof(u32), map_entries, map_file.get()) != map_entries) ||
		std::fflush(map_file.get()) != 0)
	{
		Console.Error("CHD disk image: Failed to create overlay map '%s'", m_overlay_map_path.c_str());
		FileSystem::DeleteFilePath(m_overlay_path.c_str());
		FileSystem::DeleteFilePath(m_overlay_map_path.c_str());
		return false;
	}

	return true;
}

bool ChdHddImage::LoadOverlayMap()
{
	OverlayMapHeader header;
	if (FileSystem::FSeek64(m_overlay_map_file.get(), 0, SEEK_SET) != 0 ||
		std::fread(&header, sizeof(header), 1, m_overlay_map_file.get()) != 1)
	{
		Console.Error("CHD disk image: Failed to read overlay map '%s'", m_overlay_map_path.c_str());
		return false;
	}

	if (std::memcmp(header.magic, OVERLAY_MAP_MAGIC, sizeof(header.magic)) != 0 ||
		header.version != OVERLAY_MAP_VERSION ||
		header.block_size != OVERLAY_BLOCK_SIZE ||
		header.logical_size != m_logical_size ||
		header.block_count != m_overlay_block_count ||
		header.written_block_count > m_overlay_block_count)
	{
		Console.Error("CHD disk image: overlay map '%s' does not match the selected CHD", m_overlay_map_path.c_str());
		return false;
	}

	const size_t map_entries = static_cast<size_t>(m_overlay_block_count);
	m_overlay_map.resize(map_entries);
	if (map_entries > 0 && std::fread(m_overlay_map.data(), sizeof(u32), map_entries, m_overlay_map_file.get()) != map_entries)
	{
		Console.Error("CHD disk image: Failed to read overlay map '%s'", m_overlay_map_path.c_str());
		return false;
	}
	m_overlay_written_block_count = header.written_block_count;
	std::vector<bool> used_slots(m_overlay_written_block_count, false);
	for (const u32 slot : m_overlay_map)
	{
		if (slot == OVERLAY_BLOCK_ABSENT)
			continue;
		if (slot >= m_overlay_written_block_count || used_slots[slot])
		{
			Console.Error("CHD disk image: overlay map '%s' is invalid", m_overlay_map_path.c_str());
			return false;
		}
		used_slots[slot] = true;
	}

	const s64 overlay_size = FileSystem::FSize64(m_overlay_file.get());
	if (overlay_size < 0)
	{
		Console.Error("CHD disk image: Failed to determine overlay size");
		return false;
	}
	const u64 minimum_overlay_size = static_cast<u64>(m_overlay_written_block_count) * OVERLAY_BLOCK_SIZE;
	if (static_cast<u64>(overlay_size) < minimum_overlay_size)
	{
		Console.Error("CHD disk image: overlay '%s' is incomplete", m_overlay_path.c_str());
		return false;
	}

	m_overlay_block_buffer.resize(OVERLAY_BLOCK_SIZE);
	return true;
}

bool ChdHddImage::ReadSectors(u64 sector, u32 count, void* dst)
{
	return ReadBytes(sector * SECTOR_SIZE, count * SECTOR_SIZE, static_cast<u8*>(dst));
}

bool ChdHddImage::WriteSectors(u64 sector, u32 count, const void* src)
{
	return WriteOverlayBytes(sector * SECTOR_SIZE, count * SECTOR_SIZE, static_cast<const u8*>(src));
}

bool ChdHddImage::ReadBytes(u64 offset, u32 size, u8* dst)
{
	u32 done = 0;
	while (done < size)
	{
		const u64 current_offset = offset + done;
		const u64 block_index = current_offset / OVERLAY_BLOCK_SIZE;
		const u32 block_offset = static_cast<u32>(current_offset % OVERLAY_BLOCK_SIZE);
		const u32 block_remaining = GetOverlayBlockSize(block_index) - block_offset;
		const u32 read_size = std::min(size - done, block_remaining);

		if (IsOverlayBlockPresent(block_index))
		{
			if (!ReadOverlayBytes(block_index, block_offset, read_size, dst + done))
				return false;
		}
		else if (!ReadBaseBytes(current_offset, read_size, dst + done))
		{
			return false;
		}

		done += read_size;
	}

	return true;
}

bool ChdHddImage::ReadBaseBytes(u64 offset, u32 size, u8* dst)
{
	u32 done = 0;
	while (done < size)
	{
		const u64 current_offset = offset + done;
		const u32 hunk = static_cast<u32>(current_offset / m_hunk_size);
		const u32 hunk_offset = static_cast<u32>(current_offset % m_hunk_size);
		const u32 read_size = std::min(size - done, m_hunk_size - hunk_offset);

		const u8* hunk_data;
		if (!GetCachedHunk(hunk, &hunk_data))
			return false;

		std::memcpy(dst + done, hunk_data + hunk_offset, read_size);
		done += read_size;
	}

	return true;
}

bool ChdHddImage::ReadOverlayBytes(u64 block_index, u32 block_offset, u32 size, u8* dst)
{
	const u64 slot = m_overlay_map[static_cast<size_t>(block_index)];
	const u64 overlay_offset = slot * OVERLAY_BLOCK_SIZE + block_offset;
	if (FileSystem::FSeek64(m_overlay_file.get(), overlay_offset, SEEK_SET) != 0 ||
		std::fread(dst, size, 1, m_overlay_file.get()) != 1)
	{
		Console.Error("CHD disk image: overlay read error");
		return false;
	}

	return true;
}

bool ChdHddImage::WriteOverlayBytes(u64 offset, u32 size, const u8* src)
{
	u32 done = 0;
	while (done < size)
	{
		const u64 current_offset = offset + done;
		const u64 block_index = current_offset / OVERLAY_BLOCK_SIZE;
		const u32 block_offset = static_cast<u32>(current_offset % OVERLAY_BLOCK_SIZE);
		const u32 block_size = GetOverlayBlockSize(block_index);
		const u32 write_size = std::min(size - done, block_size - block_offset);

		if (!IsOverlayBlockPresent(block_index))
		{
			if (!SetOverlayBlockPresent(block_index) ||
				!ReadBaseBytes(block_index * OVERLAY_BLOCK_SIZE, block_size, m_overlay_block_buffer.data()) ||
				!WriteOverlayBlock(block_index, m_overlay_block_buffer.data(), block_size))
			{
				return false;
			}
		}

		const u64 slot = m_overlay_map[static_cast<size_t>(block_index)];
		const u64 overlay_offset = slot * OVERLAY_BLOCK_SIZE + block_offset;
		if (FileSystem::FSeek64(m_overlay_file.get(), overlay_offset, SEEK_SET) != 0 ||
			std::fwrite(src + done, write_size, 1, m_overlay_file.get()) != 1 ||
			std::fflush(m_overlay_file.get()) != 0)
		{
			Console.Error("CHD disk image: overlay write error");
			return false;
		}

		done += write_size;
	}

	return true;
}

bool ChdHddImage::ReadOverlayBlock(u64 block_index, u8* dst, u32 block_size)
{
	return ReadOverlayBytes(block_index, 0, block_size, dst);
}

bool ChdHddImage::WriteOverlayBlock(u64 block_index, const u8* src, u32 block_size)
{
	const u64 slot = m_overlay_map[static_cast<size_t>(block_index)];
	if (FileSystem::FSeek64(m_overlay_file.get(), slot * OVERLAY_BLOCK_SIZE, SEEK_SET) != 0 ||
		std::fwrite(src, block_size, 1, m_overlay_file.get()) != 1 ||
		std::fflush(m_overlay_file.get()) != 0)
	{
		Console.Error("CHD disk image: overlay block write error");
		return false;
	}

	return true;
}

bool ChdHddImage::GetCachedHunk(u32 hunk, const u8** data)
{
	for (HunkCacheEntry& entry : m_hunk_cache)
	{
		if (entry.hunk == hunk)
		{
			entry.last_used = ++m_hunk_cache_counter;
			*data = entry.data.data();
			return true;
		}
	}

	HunkCacheEntry* target = &m_hunk_cache[0];
	for (HunkCacheEntry& entry : m_hunk_cache)
	{
		if (entry.hunk == UINT32_MAX)
		{
			target = &entry;
			break;
		}

		if (entry.last_used < target->last_used)
			target = &entry;
	}

	if (target->data.size() != m_hunk_size)
		target->data.resize(m_hunk_size);

	const chd_error err = chd_read(m_chd, hunk, target->data.data());
	if (err != CHDERR_NONE)
	{
		Console.Error("CHD disk image: hunk read error: %s", chd_error_string(err));
		return false;
	}

	target->hunk = hunk;
	target->last_used = ++m_hunk_cache_counter;
	*data = target->data.data();
	return true;
}

bool ChdHddImage::IsOverlayBlockPresent(u64 block_index) const
{
	return m_overlay_map[static_cast<size_t>(block_index)] != OVERLAY_BLOCK_ABSENT;
}

bool ChdHddImage::SetOverlayBlockPresent(u64 block_index)
{
	u32& slot = m_overlay_map[static_cast<size_t>(block_index)];
	if (slot != OVERLAY_BLOCK_ABSENT)
		return true;

	slot = m_overlay_written_block_count++;
	const s64 map_offset = sizeof(OverlayMapHeader) + static_cast<s64>(block_index * sizeof(u32));
	const s64 written_count_offset = offsetof(OverlayMapHeader, written_block_count);
	if (FileSystem::FSeek64(m_overlay_map_file.get(), map_offset, SEEK_SET) != 0 ||
		std::fwrite(&slot, sizeof(slot), 1, m_overlay_map_file.get()) != 1 ||
		FileSystem::FSeek64(m_overlay_map_file.get(), written_count_offset, SEEK_SET) != 0 ||
		std::fwrite(&m_overlay_written_block_count, sizeof(m_overlay_written_block_count), 1, m_overlay_map_file.get()) != 1 ||
		std::fflush(m_overlay_map_file.get()) != 0)
	{
		Console.Error("CHD disk image: overlay map write error");
		return false;
	}

	return true;
}

u32 ChdHddImage::GetOverlayBlockSize(u64 block_index) const
{
	const u64 block_start = block_index * OVERLAY_BLOCK_SIZE;
	return static_cast<u32>(std::min<u64>(OVERLAY_BLOCK_SIZE, m_logical_size - block_start));
}

std::string ChdHddImage::GetDefaultOverlayBasePath(const std::string& path) const
{
	u8 digest[16];
	MD5Digest md5;
	md5.Update(path.data(), static_cast<u32>(path.size()));
	md5.Final(digest);

	std::string hash;
	hash.reserve(sizeof(digest) * 2);
	for (const u8 value : digest)
		hash += fmt::format("{:02x}", value);

	std::string title(Path::GetFileTitle(path));
	Path::SanitizeFileName(&title);
	if (title.empty())
		title = "hdd";

	const std::string overlay_dir = Path::Combine(EmuFolders::DataRoot, "hdd-overlays");
	return Path::Combine(overlay_dir, fmt::format("{}-{}.overlay", title, hash));
}
