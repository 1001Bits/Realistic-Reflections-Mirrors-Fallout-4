#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

struct MirrorNativeViewport
{
    static constexpr std::size_t kDirtyOffset = 0x1B70;
    static constexpr std::size_t kViewportOffset = kDirtyOffset + 0x90;
    using Viewport = std::array<float, 6>;

    bool Save(std::uint8_t* context) noexcept
    {
        if (!context || context_)
            return false;
        std::memcpy(saved_.data(), context + kViewportOffset, sizeof(saved_));
        context_ = context;
        return true;
    }

    bool SetExtent(std::uint32_t width, std::uint32_t height) noexcept
    {
        if (!context_ || width == 0 || height == 0 || width > 16384 || height > 16384)
            return false;
        const Viewport capture{0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
        Write(capture);
        return true;
    }

    void Restore() noexcept
    {
        if (!context_)
            return;
        Write(saved_);
        context_ = nullptr;
    }

    bool Active() const noexcept { return context_ != nullptr; }

private:
    void Write(const Viewport& viewport) noexcept
    {
        std::memcpy(context_ + kViewportOffset, viewport.data(), sizeof(viewport));
        std::uint32_t dirty{};
        std::memcpy(&dirty, context_ + kDirtyOffset, sizeof(dirty));
        dirty |= 2u;
        std::memcpy(context_ + kDirtyOffset, &dirty, sizeof(dirty));
    }

    std::uint8_t* context_{};
    Viewport saved_{};
};
