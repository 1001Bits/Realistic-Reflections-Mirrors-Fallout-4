#pragma once

#include <cstddef>
#include <cstdint>
#include <windows.h>

namespace MirrorSubgraphOwnership
{
	inline constexpr std::uint32_t kMaximum = 8;
	inline constexpr std::size_t kStride = 0x48;
	struct Engine
	{
		void* singleton;
		void* (*getLoaded)(void*, const std::uint64_t*, const int*);
		void (*remove)(void*, const std::uint64_t*);
		std::uint32_t (*add)(std::uint8_t*, std::uint64_t);
		void (*lock)(void*);
		void (*unlock)(void*);
	};
	struct Entry { std::uint64_t handle; void* data; };
	struct Snapshot
	{
		Entry entries[kMaximum]{};
		std::uint32_t count = 0;
		std::uint32_t total = 0;
		bool ready = true;
	};
	enum class Result { kAdded, kDuplicate, kDeferred, kFailed, kUncertain };
	struct Receipt
	{
		Result result = Result::kDeferred;
		std::uint16_t before = 0;
		std::uint16_t after = 0;
	};
	inline bool Plausible(const void* pointer) noexcept
	{
		const auto value = reinterpret_cast<std::uintptr_t>(pointer);
		return value > 0x10000 && value <= 0x00007FFFFFFFFFFFULL && !(value & 7u);
	}
	inline std::uint32_t Count(const std::uint8_t* container) noexcept
	{
		return *reinterpret_cast<const std::uint32_t*>(container + 0x10);
	}
	inline std::uint8_t* Entries(const std::uint8_t* container) noexcept
	{
		return *reinterpret_cast<std::uint8_t* const*>(container);
	}
	
	inline void* ContainerLock(const std::uint8_t* container) noexcept
	{
		return *reinterpret_cast<void* const*>(container + 0x28);
	}

	inline bool PrepareForDestruction(std::uint8_t* container) noexcept
	{
		const auto count = Count(container);
		auto* entries = Entries(container);
		if (count > 64 || (count && !Plausible(entries))) return false;
		
		for (std::uint32_t i = 0; i < count; ++i) {
			const auto* entry = entries + i * kStride;
			if (entry[0x40] || entry[0x41]) continue;
			if (*reinterpret_cast<const std::uint64_t*>(entry) ||
				*reinterpret_cast<void* const*>(entry + 8) ||
				*reinterpret_cast<const std::uint32_t*>(entry + 0x38)) return false;
		}
		for (std::uint32_t i = 0; i < count; ++i) {
			auto* entry = entries + i * kStride;
			if (!entry[0x40] && !entry[0x41]) entry[0x41] = 1;
		}
		return true;
	}
	inline bool ReadSnapshotUnsafe(const Engine& engine, const std::uint8_t* container, Snapshot& snapshot) noexcept
	{
		if (!Plausible(container) || !Plausible(ContainerLock(container)))
			return false;
		auto* lock = ContainerLock(container);
		engine.lock(lock);
		__try {
			snapshot.total = Count(container);
			auto* entries = Entries(container);
			if (snapshot.total > 64 || (snapshot.total && !Plausible(entries)))
				return false;
			for (std::uint32_t i = 0; i < snapshot.total; ++i) {
				auto* entry = entries + i * kStride;
				if (!entry[0x40])
					continue;
				if (snapshot.count == kMaximum)
					return false; 
				auto& target = snapshot.entries[snapshot.count++];
				target = { *reinterpret_cast<const std::uint64_t*>(entry),
					*reinterpret_cast<void* const*>(entry + 8) };
				if (!target.handle || !Plausible(target.data) || entry[0x41])
					snapshot.ready = false;
			}
			return true;
		} __finally {
			engine.unlock(lock);
		}
	}
	inline bool ReadSnapshot(const Engine& engine, const std::uint8_t* container, Snapshot& snapshot) noexcept
	{
		snapshot = {};
		__try {
			return ReadSnapshotUnsafe(engine, container, snapshot);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}
	inline bool SameHandles(const Snapshot& left, const Snapshot& right) noexcept
	{
		if (left.count != right.count || left.count > kMaximum)
			return false;
		for (std::uint32_t i = 0; i < left.count; ++i) {
			bool found = false;
			for (std::uint32_t j = 0; j < right.count; ++j)
				found = found || left.entries[i].handle == right.entries[j].handle;
			if (!found) return false;
		}
		return true;
	}

	inline bool Acquire(const Engine& engine, const Entry& entry, Receipt& receipt) noexcept
	{
		if (!Plausible(engine.singleton) || !Plausible(entry.data))
			return false;
		auto* lock = static_cast<std::uint8_t*>(engine.singleton) + 0x50;
		engine.lock(lock);
		__try {
			const int priority = 7; 
			auto* loaded = engine.getLoaded(engine.singleton, &entry.handle, &priority);
			if (loaded != entry.data)
				return false; 
			auto* uses = reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(loaded) + 0x160);
			receipt.before = *uses;
			if (!*uses || *uses == UINT16_MAX)
				return false;
			receipt.after = ++*uses;
			return true;
		} __finally {
			engine.unlock(lock);
		}
	}
	inline void AppendOwned(const Engine& engine, std::uint8_t* container,
		const Entry& entry, Receipt& receipt) noexcept
	{
		const auto beforeCount = Count(container);
		bool transferred = false;
		__try {
			const auto index = engine.add(container, entry.handle);
			if (index == UINT32_MAX) {
				receipt.result = Result::kFailed;
				return;
			}
			auto* entries = Entries(container); 
			if (!Plausible(entries) || Count(container) != beforeCount + 1 || index != beforeCount ||
				*reinterpret_cast<const std::uint64_t*>(entries + index * kStride) != entry.handle) {
				receipt.result = Result::kUncertain;
				return;
			}
			auto* added = entries + index * kStride;
			if (added[0x40] || added[0x41]) {
				receipt.result = Result::kUncertain;
				return;
			}
			added[0x40] = 1;
			transferred = true; 
			receipt.result = Result::kAdded;
		} __finally {
			if (!transferred) {

				if (Count(container) == beforeCount)
					engine.remove(engine.singleton, &entry.handle);
				else
					receipt.result = Result::kUncertain;
			}
		}
	}
	inline void MirrorEntryUnsafe(const Engine& engine, std::uint8_t* container,
		const Entry& entry, Receipt& receipt) noexcept
	{
		if (!Plausible(container) || !Plausible(ContainerLock(container)))
			return;
		auto* lock = ContainerLock(container);
		engine.lock(lock);
		__try {
			const auto count = Count(container);
			auto* entries = Entries(container);
			if (count > kMaximum || (count && !Plausible(entries))) {
				receipt.result = Result::kUncertain;
				return;
			}
			for (std::uint32_t i = 0; i < count; ++i) {
				auto* existing = entries + i * kStride;
				if (*reinterpret_cast<const std::uint64_t*>(existing) == entry.handle) {
					receipt.result = existing[0x40] && !existing[0x41] ? Result::kDuplicate : Result::kDeferred;
					return;
				}
			}
			if (count == kMaximum || !Acquire(engine, entry, receipt))
				return;
			AppendOwned(engine, container, entry, receipt);
		} __finally {
			engine.unlock(lock);
		}
	}
	inline Receipt MirrorEntry(const Engine& engine, std::uint8_t* container, const Entry& entry) noexcept
	{
		Receipt receipt{};
		__try {
			MirrorEntryUnsafe(engine, container, entry, receipt);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			receipt.result = Result::kUncertain;
		}
		return receipt;
	}
}
