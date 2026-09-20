#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include "MirrorCaptureExtent.h"

template<class Resource, std::size_t Slots = 4>
class MirrorCaptureResourcePool
{
public:
	static constexpr std::uint64_t Budget = 256ull * 1024 * 1024;
	static constexpr std::uint64_t KeepMs = 15000;

	static constexpr std::uint64_t Bytes(MirrorCaptureExtent size) noexcept
	{
		return size.Pixels() * 48u + std::uint64_t((size.width + 31) / 32) * ((size.height + 31) / 32) * 36u + 4096u;
	}
	static MirrorCaptureExtent Shape(const Resource& resource) noexcept { return {resource.Width(), resource.Height()}; }
	std::uint64_t RetainedBytes() const noexcept
	{
		std::uint64_t bytes = 0;
		for (const auto& entry : entries_) if (entry.resource.Ready()) bytes += Bytes(Shape(entry.resource));
		return bytes;
	}
	bool Has(MirrorCaptureExtent size) const noexcept
	{
		for (const auto& entry : entries_) if (entry.resource.Ready() && Shape(entry.resource) == size) return true;
		return false;
	}
	MirrorCaptureExtent AvailableSize(MirrorCaptureExtent wanted, MirrorCaptureExtent current) const noexcept
	{
		if (current) return Has(wanted) ? wanted : current;
		MirrorCaptureExtent starter{};
		for (const auto& entry : entries_) {
			const auto size = Shape(entry.resource);
			if (entry.resource.Ready() && size.Fits(wanted) && size.Maximum() <= 1024 && size.Pixels() > starter.Pixels()) starter = size;
		}
		return starter;
	}
	void Request(MirrorCaptureExtent size, std::uint64_t now) noexcept
	{
		if (!size.Poolable()) return;
		for (auto& entry : entries_) if (entry.resource.Ready() && Shape(entry.resource) == size) {
			entry.touched = now; return;
		}
		for (const auto pending : requests_) if (pending == size) return;
		for (auto& pending : requests_) if (!pending) { pending = size; return; }
	}
	template<class Device>
	void Maintain(Device* device, std::uint64_t now) noexcept
	{
		for (auto& entry : entries_) {
			if (entry.resource.Ready() && (!entry.resource.UsesDevice(device) ||
				(now >= entry.touched && now - entry.touched > KeepMs))) entry.resource.Release();
		}
	}

	template<class Create>
	bool Prepare(std::uint64_t now, std::uint64_t frame, Create&& create) noexcept
	{
		if (frame == preparedFrame_ || now < retryAt_) return false;
		for (auto& pending : requests_) {
			if (!pending) continue;
			if (Has(pending)) { pending = 0; continue; }
			const auto size = pending;
			auto* entry = MakeRoom(Bytes(size), 0);
			if (!entry) return false;
			preparedFrame_ = frame;
			if (!create(entry->resource, size)) { entry->resource.Release(); retryAt_ = now + 1000; return false; }
			entry->touched = now; pending = 0;
			return true;
		}
		return false;
	}
	bool Acquire(Resource& target, MirrorCaptureExtent size, std::uint64_t now) noexcept
	{
		if (target.Ready()) return false;
		for (auto& entry : entries_) {
			if (!entry.resource.Ready() || Shape(entry.resource) != size) continue;
			if (!target.SwapUnbound(entry.resource)) return false;
			entry.touched = now;
			return true;
		}
		return false;
	}
	bool Park(Resource& target, std::uint64_t now, MirrorCaptureExtent protectSize) noexcept
	{
		if (!target.Ready()) return true;
		if (!Has(Shape(target))) {
			if (auto* entry = MakeRoom(Bytes(Shape(target)), protectSize)) {
				if (target.SwapUnbound(entry->resource)) { entry->touched = now; return true; }
			}
		}
		return false;
	}
	void Clear() noexcept
	{
		for (auto& entry : entries_) entry.resource.Release();
		requests_ = {}; retryAt_ = 0;
	}
private:
	struct Entry { Resource resource; std::uint64_t touched{}; };
	Entry* MakeRoom(std::uint64_t bytes, MirrorCaptureExtent protectSize) noexcept
	{
		if (bytes > Budget) return nullptr;
		for (;;) {
			Entry* empty = nullptr;
			Entry* oldest = nullptr;
			for (auto& entry : entries_) {
				if (!entry.resource.Ready()) empty = &entry;
				else if (Shape(entry.resource) != protectSize && (!oldest || entry.touched < oldest->touched)) oldest = &entry;
			}
			if (empty && RetainedBytes() + bytes <= Budget) return empty;
			if (!oldest) return nullptr;
			oldest->resource.Release();
		}
	}
	std::array<Entry, Slots> entries_;
	std::array<MirrorCaptureExtent, Slots> requests_{};
	std::uint64_t retryAt_{}, preparedFrame_{~std::uint64_t{}};
};
