#pragma once

#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <span>
#include <sstream>
#include <string>

namespace MirrorHookDiagnostics
{
    inline bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept
    {
        if (!output || !size || address <= 0x10000 ||
            size > (std::numeric_limits<std::uintptr_t>::max)() - address) return false;
        const auto end = address + size;
        for (auto cursor = address; cursor < end;) {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)) ||
                region.State != MEM_COMMIT || (region.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
            const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            if (region.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - base) return false;
            const auto next = base + region.RegionSize;
            if (next <= cursor) return false;
            cursor = next;
        }
        SIZE_T copied{};
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
            output, size, &copied) && copied == size;
    }

    inline std::string Hex(std::span<const std::uint8_t> bytes)
    {
        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (auto byte : bytes) out << std::setw(2) << unsigned(byte);
        return out.str();
    }

    inline bool Matches(std::uintptr_t address, std::span<const std::uint8_t> expected) noexcept
    {
        std::array<std::uint8_t, 48> actual{};
        return !expected.empty() && expected.size() <= actual.size() &&
            Read(address, actual.data(), expected.size()) &&
            std::memcmp(actual.data(), expected.data(), expected.size()) == 0;
    }

    inline std::uintptr_t Relative(std::uintptr_t next, std::int32_t displacement) noexcept
    {
        const auto distance = displacement < 0 ? -std::int64_t(displacement) : std::int64_t(displacement);
        if (displacement < 0) return std::uint64_t(distance) <= next ? next - distance : 0;
        return std::uint64_t(distance) <= (std::numeric_limits<std::uintptr_t>::max)() - next ? next + distance : 0;
    }

    inline std::uintptr_t Jump(std::uintptr_t address, const std::array<std::uint8_t, 16>& bytes) noexcept
    {
        if (address > (std::numeric_limits<std::uintptr_t>::max)() - bytes.size()) return 0;
        if (bytes[0] == 0xe9) {
            std::int32_t relative{}; std::memcpy(&relative, bytes.data() + 1, sizeof(relative));
            return Relative(address + 5, relative);
        }
        if (bytes[0] == 0xeb) return Relative(address + 2, static_cast<std::int8_t>(bytes[1]));
        if (bytes[0] == 0xff && bytes[1] == 0x25) {
            std::int32_t relative{}; std::memcpy(&relative, bytes.data() + 2, sizeof(relative));
            std::uintptr_t target{};
            return Read(Relative(address + 6, relative), &target, sizeof(target)) ? target : 0;
        }
        if ((bytes[0] == 0x48 && bytes[1] == 0xb8 && bytes[10] == 0xff && bytes[11] == 0xe0) ||
            (bytes[0] == 0x49 && bytes[1] == 0xbb && bytes[10] == 0x41 && bytes[11] == 0xff && bytes[12] == 0xe3)) {
            std::uintptr_t target{}; std::memcpy(&target, bytes.data() + 2, sizeof(target));
            return target;
        }
        return 0; 
    }

    inline std::string Describe(std::uintptr_t address)
    {
        std::ostringstream out;
        std::array<std::uintptr_t, 4> visited{};
        for (unsigned index = 0; index < visited.size(); ++index) {
            if (index) out << " -> ";
            out << "0x" << std::hex << address;
            for (unsigned prior = 0; prior < index; ++prior)
                if (visited[prior] == address) { out << " [cycle]"; return out.str(); }
            visited[index] = address;
            HMODULE module{};
            if (address && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(address), &module)) {
                std::array<char, 1024> path{};
                const auto length = GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
                if (length && length < path.size()) {
                    const auto* name = std::strrchr(path.data(), '\\');
                    out << " [" << (name ? name + 1 : path.data()) << "+0x"
                        << address - reinterpret_cast<std::uintptr_t>(module) << ']';
                } else out << " [module-name-unavailable]";
            } else out << " [no-module]";
            std::array<std::uint8_t, 16> bytes{};
            if (!Read(address, bytes.data(), bytes.size())) { out << " [unreadable]"; break; }
            out << " bytes=" << Hex(bytes);
            const auto next = Jump(address, bytes);
            if (!next) break;
            address = next;
            if (index + 1 == visited.size()) out << " [hop-limit]";
        }
        return out.str();
    }
}
