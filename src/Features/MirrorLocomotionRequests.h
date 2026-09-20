#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MirrorLocomotion
{

    struct Request
    {
        std::array<char, 128> name{};
        std::uint32_t value{};
        std::uint8_t type{};
        bool variable{};
    };

    struct Requests
    {
        std::array<Request, 64> items{};
        std::size_t count{};
        bool valid{true};
        bool sampled{};

        bool SameState(const Requests& previous) const noexcept
        {
            if (!valid || !previous.valid || count != previous.count) return false;
            for (std::size_t i = 0; i < count; ++i) {
                const auto& a = items[i]; const auto& b = previous.items[i];
                if (a.name != b.name || a.variable != b.variable || a.type != b.type ||
                    (a.variable && a.type == 0 && a.value != b.value)) return false;
            }
            return true;
        }

        template<class T> static T Read(const void* base, std::size_t offset) noexcept
        {
            T value;
            std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(value));
            return value;
        }

        template<class Name> bool CaptureUnsafe(const void* arena, Name&& nameOf)
        {
            if (!valid || !arena) return valid = false;
            const auto size = Read<std::uint32_t>(arena, 0x38);
            if (size > items.size() - count) return valid = false;
            auto* block = Read<const std::byte*>(arena, 8);
            auto* record = Read<const std::byte*>(arena, 0x30);
            const auto begin = count;
            for (std::uint32_t i = 0; i < size; ++i) {
                if (block && record == block + 0x100) {
                    block = Read<const std::byte*>(block, 0x100);
                    record = block;
                }
                const auto address = reinterpret_cast<std::uintptr_t>(record);
                const auto base = reinterpret_cast<std::uintptr_t>(block);
                if (!block || !record || address < base || address - base >= 0x100 ||
                    (address - base) % 16 != 0) return valid = false;
                auto& request = items[begin + i];
                const auto type = Read<std::uint8_t>(record, 12);
                const auto variable = Read<std::uint8_t>(record, 13);
                request.value = Read<std::uint32_t>(record, 8);
                if (variable > 1 || (variable && (type > 1 ||
                    (type == 1 && !std::isfinite(std::bit_cast<float>(request.value)))))) return valid = false;
                request.type = type;
                request.variable = variable != 0;
                const char* name = nameOf(record);
                if (!name || !*name) return valid = false;
                std::size_t n = 0;
                for (; n < request.name.size() - 1 && name[n]; ++n) request.name[n] = name[n];
                if (name[n]) return valid = false;
                request.name[n] = '\0';
                record += 16;
            }
            count += size;
            sampled = true;
            return true;
        }

        template<class Apply> bool Flush(Apply&& apply) const
        {
            if (!valid) return false;
            for (std::size_t i = 0; i < count; ++i) apply(items[i]);
            return true;
        }

        template<class Apply> bool ApplyTo(Requests& previous, Apply&& apply) const
        {
            if (!valid) return false;
            if (!sampled) return true;
            const bool sameState = SameState(previous);
            bool accepted = true;
            for (std::size_t i = 0; i < count; ++i) {
                if (items[i].variable || !sameState)
                {
                    const bool applied = apply(items[i], sameState);
                    if (items[i].variable) accepted = applied && accepted;
                }
            }
            if (accepted) previous = *this;
            return true;
        }
    };

    template<class String> struct ArenaView
    {
        struct Record { String name{}; std::uint32_t value{}; std::uint8_t type{}, variable{}; std::uint16_t pad{}; };
        static_assert(sizeof(Record) == 16);
        struct Block { std::array<Record, 16> records{}; Block* next{}; };
        static_assert(sizeof(Block) == 0x108);
        struct Header {
            void* allocator{}; Block* first{}; void* tailLink{}; Block* last{}; Block* spare{};
            Record* back{}; Record* front{}; std::uint32_t count{}, pad{};
        } header{};
        static_assert(offsetof(Header, count) == 0x38);
        std::array<Block, 4> blocks{};

        explicit ArenaView(const Requests* previous)
        {
            if (!previous || !previous->valid || previous->count > 64 || !previous->count) return;
            header.count = static_cast<std::uint32_t>(previous->count);
            header.first = blocks.data(); header.front = blocks[0].records.data();
            const auto last = (previous->count - 1) / 16;
            header.last = &blocks[last]; header.tailLink = &blocks[last].next;
            header.back = blocks[last].records.data() + (previous->count - last * 16);
            for (std::size_t i = 0; i < last; ++i) blocks[i].next = &blocks[i + 1];
            for (std::size_t i = 0; i < previous->count; ++i) {
                const auto& source = previous->items[i]; auto& destination = blocks[i / 16].records[i % 16];
                destination.name = source.name.data(); destination.value = source.value;
                destination.type = source.type; destination.variable = source.variable;
            }
        }
    };
}
