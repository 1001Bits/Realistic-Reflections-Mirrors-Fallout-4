#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MirrorForwardLights
{

    struct Inputs
    {
        alignas(16) std::array<std::byte, 0x38> pass{};
        std::array<const void*, 7> list{};
        std::array<std::array<std::byte, 0xC0>, 6> lights{};

        bool Prepare(const void* source, int count) noexcept
        {
            if (!source || count <= 0 || count > 6) return false;
            std::memcpy(pass.data(), source, pass.size());
            const void* const* original{};
            std::memcpy(&original, pass.data()+0x30, sizeof(original));
            if (reinterpret_cast<std::uintptr_t>(original) <= 0x10000) return false;
            list[0]=original[0];
            for (int i=0; i<count; ++i) {
                if (reinterpret_cast<std::uintptr_t>(original[i+1]) <= 0x10000) return false;
                std::memcpy(lights[i].data(), original[i+1], lights[i].size());
                const float reflectedFade=1.0f;
                std::memcpy(lights[i].data()+0x10, &reflectedFade, sizeof(reflectedFade));
                list[i+1]=lights[i].data();
            }
            const auto* replacement=list.data();
            std::memcpy(pass.data()+0x30, &replacement, sizeof(replacement));
            return true;
        }
    };
}
