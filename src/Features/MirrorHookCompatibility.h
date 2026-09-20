#pragma once

#include "MirrorHookDiagnostics.h"
#include <detours/detours.h>

namespace MirrorHookCompatibility
{
    enum class Entry { Rejected, Native, Chained };

    inline bool Executable(std::uintptr_t address, std::size_t size) noexcept
    {
        if (address <= 0x10000 || !size ||
            size > (std::numeric_limits<std::uintptr_t>::max)() - address) return false;
        for (auto cursor = address; cursor < address + size;) {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)) ||
                region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
            const auto protection = region.Protect & 0xff;
            if (protection != PAGE_EXECUTE && protection != PAGE_EXECUTE_READ &&
                protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
            const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            if (region.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - base) return false;
            const auto next = base + region.RegionSize;
            if (next <= cursor) return false;
            cursor = next;
        }
        return true;
    }

    inline bool CodeChain(std::uintptr_t address, std::span<const std::uintptr_t> forbidden) noexcept
    {
        std::array<std::uintptr_t, 8> visited{};
        for (std::size_t index = 0; index < visited.size(); ++index) {
            for (const auto target : forbidden) if (address == target) return false;
            for (std::size_t prior = 0; prior < index; ++prior) if (address == visited[prior]) return false;
            visited[index] = address;
            std::array<std::uint8_t, 16> bytes{};
            if (!Executable(address, bytes.size()) ||
                !MirrorHookDiagnostics::Read(address, bytes.data(), bytes.size())) return false;
            const bool relay = bytes[0] == 0xe9 || bytes[0] == 0xeb ||
                (bytes[0] == 0xff && bytes[1] == 0x25) ||
                (bytes[0] == 0x48 && bytes[1] == 0xb8 && bytes[10] == 0xff && bytes[11] == 0xe0) ||
                (bytes[0] == 0x49 && bytes[1] == 0xbb && bytes[10] == 0x41 && bytes[11] == 0xff && bytes[12] == 0xe3);
            if (!relay) return bytes[0] != 0xcc && bytes[0] != 0; 
            address = MirrorHookDiagnostics::Jump(address, bytes);
            if (!address) return false;
        }
        return false;
    }

    inline Entry InspectEntry(std::uintptr_t address, std::span<const std::uint8_t> expected,
        std::span<const std::uintptr_t> forbidden) noexcept
    {
        if (expected.empty() || expected.size() > 48 || !CodeChain(address, forbidden)) return Entry::Rejected;
        const bool native = MirrorHookDiagnostics::Matches(address, expected);
        if (!native) {
            std::array<std::uint8_t, 6> bytes{};
            if (!MirrorHookDiagnostics::Read(address, bytes.data(), bytes.size()) ||
                !(bytes[0] == 0xe9 || (bytes[0] == 0xff && bytes[1] == 0x25))) return Entry::Rejected;
        }

        if (DetourCodeFromPointer(reinterpret_cast<void*>(address), nullptr) != reinterpret_cast<void*>(address))
            return Entry::Rejected;
        return native ? Entry::Native : Entry::Chained;
    }
}
