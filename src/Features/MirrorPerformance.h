#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "MirrorOverlayMetrics.h"
struct ID3D11DeviceContext;

namespace MirrorPerformance
{
	enum class Stage : unsigned { FlatNative, FlatMaterial, VRStereo, FlatComposite, VRComposite, FlatService, VRService, FlatShadowCopy, FlatLightingPrepare, FlatPrivateSun, FlatSunCollect, FlatSunDraw, FlatSunArm, FlatSunSubmit, FlatSunCull, FlatSunCache, FlatSunAccumulate, FlatSunRaster, FlatSunRetire, FlatResolve, Count };
	
	class Sample
	{
	public:
		explicit Sample(Stage stage, unsigned slot = 0, ID3D11DeviceContext* context = nullptr) noexcept;
		~Sample();
		Sample(const Sample&) = delete;
		Sample& operator=(const Sample&) = delete;
		void Finish(bool completed = true) noexcept;
	private:
		std::int64_t start_{};
		std::uint64_t phase_{};
		unsigned tag_{};
		int gpu_{ -1 };
		bool active_{ true };
	};
	class AnimationSample
	{
	public:
		AnimationSample() noexcept;
		~AnimationSample();
	private:
		std::int64_t start_{};
		std::uint64_t phase_{};
	};

	class WorkerSample
	{
	public:
		explicit WorkerSample(Stage stage) noexcept;
		~WorkerSample();
	private:
		std::int64_t start_{};
		std::uint64_t phase_{};
		unsigned tag_{};
	};
	void SetEnabled(bool enabled) noexcept;
	void Published(unsigned slot, std::uint64_t count = 1u) noexcept;
	void PublishedReceiver(unsigned id, unsigned width, unsigned height, unsigned sourceFrame,
		std::uint64_t pose, bool playerRequired, bool playerSubmitted) noexcept;
	void SourceFrame(unsigned frame, bool worldActive) noexcept;
	void StorageAllocation(unsigned width, unsigned height, double ms, bool success, bool pooled, std::uint64_t retainedBytes) noexcept;

	void ShadowMap(unsigned map, bool reused) noexcept;
	void CaptureResult(unsigned slot, bool returned, unsigned faces, bool clipped, bool playerUnavailable, double cullMs, double drawMs) noexcept;
	struct SceneCollectionCounts
	{
		std::uint32_t visited{}, boundsTested{}, rejected{}, partitioned{}, rangeRejected{}, distantRoots{}, tinyRejected{};
	};
	void SceneCollected(unsigned slot, const SceneCollectionCounts& counts, std::uint32_t retained) noexcept;
	void Tick() noexcept;
	
	void FramePresented(bool worldActive) noexcept;
	
	OverlaySnapshot GetOverlaySnapshot() noexcept;
	
	void FrameRendered(bool worldActive) noexcept;

	struct Breakdown
	{
		bool valid{};
		bool detailed{};  
		double seconds{}, capturesPerSecond{};
		double capture{}, shadows{}, culling{}, drawing{}, other{};  
		double ownDrawWork{}, drawCommits{};                         
		double lighting{};                                           
		double lightingGpu{ -1.0 };                                  
		double gpu{ -1.0 };                                          
		double servicePerFrame{}, compositePerFrame{}, mainViewOwnWorkPerFrame{};
		double movingShare{ -1.0 };                                  
	};
	Breakdown GetBreakdown() noexcept;
	void SetDetailedTiming(bool enabled) noexcept;
	bool DetailedTimingEnabled() noexcept;
	std::int64_t Ticks() noexcept;
	
	void OwnDrawWork(bool capture, std::int64_t ticks) noexcept;
	
	void CameraMoved(bool moving) noexcept;
	
	bool BenchmarkActive() noexcept;
	bool BenchmarkWantsMirrorsOn() noexcept;
	
	void RequestBenchmark() noexcept;
	
	bool BenchmarkShowing() noexcept;
	void DismissBenchmark() noexcept;
	using BenchmarkLine = std::array<char, 112>;
	std::size_t BenchmarkLines(BenchmarkLine* lines, std::size_t capacity) noexcept;

	class OwnWorkScope
	{
	public:
		explicit OwnWorkScope(bool capture) noexcept : on_(DetailedTimingEnabled()), capture_(capture), start_(on_ ? Ticks() : 0) {}
		~OwnWorkScope() { if (on_) OwnDrawWork(capture_, Ticks() - start_ - native_); }
		OwnWorkScope(const OwnWorkScope&) = delete;
		OwnWorkScope& operator=(const OwnWorkScope&) = delete;
		void BeginNative() noexcept { if (on_) nativeStart_ = Ticks(); }
		void EndNative() noexcept { if (on_) native_ += Ticks() - nativeStart_; }
	private:
		bool on_{}, capture_{};
		std::int64_t start_{}, nativeStart_{}, native_{};
	};
}
