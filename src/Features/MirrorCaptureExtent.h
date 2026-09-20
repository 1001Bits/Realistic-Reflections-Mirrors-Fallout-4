#pragma once
#include <algorithm>
#include <cstdint>

struct MirrorCaptureExtent
{
    unsigned width{}, height{};
    constexpr MirrorCaptureExtent() noexcept = default;
    constexpr MirrorCaptureExtent(unsigned square) noexcept : width(square), height(square) {}
    constexpr MirrorCaptureExtent(unsigned w, unsigned h) noexcept : width(w), height(h) {}
    constexpr bool operator==(const MirrorCaptureExtent&) const noexcept = default;
    constexpr explicit operator bool() const noexcept { return width && height; }
    constexpr unsigned Maximum() const noexcept { return (std::max)(width, height); }
    constexpr std::uint64_t Pixels() const noexcept { return std::uint64_t(width) * height; }
    constexpr bool Fits(MirrorCaptureExtent other) const noexcept
    { return width <= other.width && height <= other.height; }
    constexpr bool Poolable() const noexcept
    { return width >= 512 && height >= 512 && Maximum() <= 2048 && width % 512 == 0 && height % 512 == 0; }
};
