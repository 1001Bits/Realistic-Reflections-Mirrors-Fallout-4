#pragma once
#include <cmath>
#include <cstdint>
#include "MirrorFrameHistogram.h"

namespace MirrorPerformance
{

    struct Average
    {
        std::uint64_t frames{};
        double milliseconds{};
        
        double p95Ms{}, p99Ms{};
        double FPS() const noexcept { return milliseconds > 0 ? 1000.0 * double(frames) / milliseconds : 0; }
        double Milliseconds() const noexcept { return frames ? milliseconds / double(frames) : 0; }
        double Seconds() const noexcept { return milliseconds / 1000.0; }
        void Add(double interval) noexcept { ++frames; milliseconds += interval; }
    };

    struct OverlaySnapshot
    {
        double fps{}, frameMs{}, maximumMs{}, publicationsHz{};
        bool valid{};
        
        Average modeOn{}, modeOff{};
        bool mirrorsOn{};
        Average Mode(bool on) const noexcept { return on ? modeOn : modeOff; }
    };

    class OverlayMetrics
    {
    public:
        void Observe(double nowMs, bool worldActive, std::uint64_t phase, std::uint64_t publications,
            bool mirrorsOn) noexcept
        {
            if (!worldActive || !std::isfinite(nowMs)) { Reset(); return; }

            if (!armed_ || phase != phase_ || nowMs <= previousMs_ || mirrorsOn != mirrorsOn_ ||
                publications < publications_) {
                if (mirrorsOn != mirrorsOn_) {
                    (mirrorsOn ? modeOn_ : modeOff_) = {};
                    (mirrorsOn ? histogramOn_ : histogramOff_).Clear();
                }
                ResetWindow();   
                armed_ = true; phase_ = phase; previousMs_ = nowMs; publications_ = publications;
                settleUntilMs_ = nowMs + 1000.0;
                mirrorsOn_ = mirrorsOn;
                PublishModes();
                return;
            }
            const double delta = nowMs - previousMs_;
            const bool settling = previousMs_ < settleUntilMs_;
            previousMs_ = nowMs;

            if (settling) { publications_ = publications; return; }
            elapsedMs_ += delta; ++frames_;
            (mirrorsOn ? modeOn_ : modeOff_).Add(delta);
            (mirrorsOn ? histogramOn_ : histogramOff_).Add(delta);
            if (delta > maximumMs_) maximumMs_ = delta;
            if (elapsedMs_ < 500.0) { PublishModes(); return; }
            modeOn_.p95Ms = histogramOn_.Percentile(0.95); modeOn_.p99Ms = histogramOn_.Percentile(0.99);
            modeOff_.p95Ms = histogramOff_.Percentile(0.95); modeOff_.p99Ms = histogramOff_.Percentile(0.99);
            snapshot_ = { 1000.0 * double(frames_) / elapsedMs_, elapsedMs_ / double(frames_), maximumMs_,
                1000.0 * static_cast<double>(publications - publications_) / elapsedMs_, true,
                modeOn_, modeOff_, mirrorsOn_ };
            elapsedMs_ = maximumMs_ = 0; frames_ = 0; publications_ = publications;
        }
        OverlaySnapshot Snapshot() const noexcept { return snapshot_; }

    private:
        void Reset() noexcept
        {
            ResetWindow();
            modeOn_ = modeOff_ = {};
            histogramOn_.Clear(); histogramOff_.Clear();
        }
        
        void ResetWindow() noexcept
        {
            snapshot_ = {}; previousMs_ = elapsedMs_ = maximumMs_ = settleUntilMs_ = 0;
            phase_ = publications_ = frames_ = 0; armed_ = false;
        }
        
        void PublishModes() noexcept
        {
            snapshot_.modeOn = modeOn_; snapshot_.modeOff = modeOff_; snapshot_.mirrorsOn = mirrorsOn_;
        }
        OverlaySnapshot snapshot_;
        Average modeOn_{}, modeOff_{};
        FrameHistogram histogramOn_{}, histogramOff_{};
        double previousMs_{}, elapsedMs_{}, maximumMs_{}, settleUntilMs_{};
        std::uint64_t phase_{}, publications_{}, frames_{};
        bool armed_{}, mirrorsOn_{};
    };
}
