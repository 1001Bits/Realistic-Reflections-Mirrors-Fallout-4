#include "MirrorPerformance.h"
#include "MirrorBenchmark.h"
#include "MirrorCaptureEvidence.h"
#include "MirrorCaptureOptimizations.h"
#include "MirrorQualityPreset.h"
#include <cstdio>
#include "MirrorToggle.h"
#include "MirrorGpuTimer.h"
#include "MirrorFrameTiming.h"
#include "MirrorSettings.h"
#include "MirrorSettingsPolicy.h"
#include "../Globals.h"

namespace
{
	MirrorGpuTimer gpu;
	struct Totals
	{
		std::uint64_t attempts{}, completed{}, gpuCount{}, gpuUnavailable{};
		double cpuSum{}, cpuMax{}, gpuSum{};
	};
	std::array<Totals, static_cast<unsigned>(MirrorPerformance::Stage::Count) * 3> totals{};
	decltype(totals) workerTotals{}; 
	std::array<std::uint64_t, 3> publications{};
	std::atomic_uint64_t overlayPublications{};
	std::atomic_uint64_t qualitySceneCaptures{}, qualitySceneNodes{};
	std::atomic_uint64_t qualityBodyFaults{};
	MirrorPerformance::CaptureEvidence captureEvidence;
	MirrorPerformance::OverlayMetrics overlayMetrics;
	std::array<std::uint64_t, 3> shadowRenders{}, shadowReuses{};
	struct CaptureTotals
	{
		std::uint64_t attempts{}, faults{}, zeroDraw{}, unclipped{}, playerUnavailable{};
		double cullSum{}, cullMax{}, drawSum{}, drawMax{};
	};
	std::array<CaptureTotals, 3> captures{};
	struct CollectionTotals
	{
		std::uint64_t count{}, visited{}, boundsTested{}, rejected{}, partitioned{}, retained{}, rangeRejected{}, distantRoots{}, tinyRejected{};
	};
	std::array<CollectionTotals, 3> collections{};

	std::mutex animationMutex;
	std::uint64_t animationTicks{}, animationCount{};
	std::atomic<std::uint64_t> phase{}; 
	std::uint64_t phaseStartedMs{}, previousReportMs{};
	std::uint64_t sampleFrame = 0;
	bool gpuSampleTaken = false;
	const std::int64_t frequency = [] { LARGE_INTEGER value{}; QueryPerformanceFrequency(&value); return value.QuadPart; }();
	std::int64_t Now() noexcept { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart; }
	double Milliseconds(std::int64_t ticks) noexcept { return 1000.0 * static_cast<double>(ticks) / static_cast<double>(frequency); }

	struct LiveTotals
	{
		std::uint64_t captures{}, gpuCount{}, lightingGpuCount{}, services{}, composites{};
		double capture{}, shadows{}, culling{}, drawing{}, lighting{}, gpu{}, lightingGpu{}, service{}, composite{};
	};
	LiveTotals live{};
	std::int64_t liveStart{};
	std::atomic<std::int64_t> ownCaptureTicks{}, ownViewTicks{};
	std::atomic<std::uint64_t> ownCaptureCommits{};
	std::atomic<std::uint64_t> presentedFrames{}, observedFrames{}, movingFrames{};
	std::atomic_bool detailedTiming{}, cameraMoving{};
	std::atomic_bool benchmarkActive{}, benchmarkWantsOn{ true };
	std::mutex breakdownMutex;
	MirrorPerformance::Breakdown breakdown{};

	bool IsCaptureTag(unsigned tag) noexcept
	{
		const auto stage = static_cast<MirrorPerformance::Stage>(tag / 3u);
		return stage == MirrorPerformance::Stage::FlatNative || stage == MirrorPerformance::Stage::FlatMaterial;
	}

	struct BenchmarkCapture
	{
		MirrorPerformance::CaptureEvidence evidence;
		std::uint64_t captures{}, gpuCount{}, lightingGpuCount{}, services{};
		
		std::uint64_t attempts{}, empty{};
		double capture{}, shadows{}, culling{}, drawing{}, lighting{}, gpu{}, lightingGpu{}, service{};
	};
	struct BenchmarkResult
	{
		bool valid{}, rendering{};
		std::uint32_t resolution{}, refreshHz{};
		MirrorPerformance::StepFrames frames{};
		BenchmarkCapture capture{};
		double seconds{};
	};
	std::mutex benchmarkMutex;               
	BenchmarkCapture benchmarkCapture{};
	std::atomic_bool benchmarkMeasuring{}, benchmarkRequested{};
	MirrorPerformance::BenchmarkClock benchmarkClock;
	MirrorPerformance::StepFrames benchmarkFrames{};
	std::array<BenchmarkResult, MirrorPerformance::kBenchmarkSteps.size()> benchmarkResults{};
	std::int64_t benchmarkMeasureStart{};
	bool benchmarkShowing{}, benchmarkFinished{};
	bool benchmarkDriftAcceptable{}, benchmarkImageComplete{};
	const char* benchmarkAbortReason{};
	struct SavedOptimizations
	{
		bool master{}, roomReuse{}, screenSizedCapture{}, tinyObjectSkip{}, paneCrop{}, rectangularCapture{};
		bool valid{};
	} benchmarkSaved{};

	void AddBenchmark(auto&& update) noexcept
	{
		if (!benchmarkMeasuring.load(std::memory_order_relaxed)) return;
		std::lock_guard lock(benchmarkMutex);
		if (!benchmarkMeasuring.load(std::memory_order_acquire)) return;
		update(benchmarkCapture);
	}

	void SaveOptimizations() noexcept
	{
		using namespace MirrorCaptureOptimizations;
		benchmarkSaved = { master.load(), roomReuse.load(), screenSizedCapture.load(),
			tinyObjectSkip.load(), paneCrop.load(), rectangularCapture.load(), true };
	}
	void RestoreBenchmarkSettings() noexcept
	{
		using namespace MirrorCaptureOptimizations;
		benchmarkMeasuring.store(false, std::memory_order_release);
		MirrorSettings::SetTemporaryValues(nullptr);
		if (!benchmarkSaved.valid) return;
		master.store(benchmarkSaved.master);
		roomReuse.store(benchmarkSaved.roomReuse);
		screenSizedCapture.store(benchmarkSaved.screenSizedCapture);
		tinyObjectSkip.store(benchmarkSaved.tinyObjectSkip);
		paneCrop.store(benchmarkSaved.paneCrop);
		rectangularCapture.store(benchmarkSaved.rectangularCapture);
		benchmarkSaved.valid = false;
	}
	void ApplyBenchmarkStep(const MirrorPerformance::BenchmarkStep& step) noexcept
	{
		using namespace MirrorCaptureOptimizations;
		
		const auto preset = MirrorQualityPreset::For(MirrorQualityPreset::Clamp(step.level));
		MirrorSettings::TemporaryValues values{ preset.resolution, preset.refreshRate, preset.objectDistance,
			preset.shadowDistance, preset.shadows, step.level == 0 };
		if (step.kind == MirrorPerformance::StepKind::kManual) {
			values.resolution = step.resolution;
			values.refreshRate = step.refreshHz;
		}
		MirrorSettings::SetTemporaryValues(&values);
		master.store(true);
		roomReuse.store(step.roomReuse);
		screenSizedCapture.store(step.screenSizedCapture);
		tinyObjectSkip.store(step.tinyObjectSkip);
		paneCrop.store(step.paneCrop);
		rectangularCapture.store(step.rectangularCapture);
		benchmarkWantsOn.store(step.kind != MirrorPerformance::StepKind::kOff, std::memory_order_release);
	}

	void LogCaptureEvidence(const char* scope, std::size_t step, const MirrorPerformance::CaptureEvidence& e, double seconds)
	{
		logger::info("[Mirrors {}] evidence step={} complete={} missingBody={} overflow={} retainedPresentationBytes={} sourceFrames={} sourceFrameAvgMs={:.3f} sourceP95Ms={:.1f} sourceP99Ms={:.1f}; raw source-frame cadence, no drift correction",
			scope, step, e.Complete(), e.bodyMissing, e.overflow, e.retainedBytes, e.sourceFrames,
			e.sourceFrames ? e.sourceTotalMs / e.sourceFrames : 0.0,
			e.sourceHistogram.Percentile(.95), e.sourceHistogram.Percentile(.99));
		for (const auto& s : e.shapes) if (s.width)
			logger::info("[Mirrors {}] target step={} actual={}x{} publications={} presentationAllocations={} poolHits={} allocationFailures={} allocationTotalMs={:.3f} allocationMaxMs={:.3f}",
				scope, step, s.width, s.height, s.publications, s.allocations, s.poolHits, s.allocationFailures, s.allocationMs, s.worstAllocationMs);
		for (const auto& r : e.receivers) if (r.id)
			logger::info("[Mirrors {}] receiver step={} id={:08X} publications={} perSecond={:.2f} activeCadenceHz={:.2f} maxGapMs={:.2f} sourceFrame={} pose={} bodyRequired={} bodySubmitted={}",
				scope, step, r.id, r.publications, seconds > 0 ? r.publications / seconds : 0.0,
				r.lastMs > r.firstMs ? 1000.0 * (r.publications - 1) / (r.lastMs - r.firstMs) : 0.0,
				r.maxGapMs, r.lastSourceFrame, r.lastPose, r.bodyRequired, r.bodySubmitted);
	}

	void LogBenchmarkStep(std::size_t index, const BenchmarkResult& r) noexcept
	{
		try {
			const auto& step = MirrorPerformance::kBenchmarkSteps[index];
			const auto& c = r.capture;
			const double captures = double(c.captures);
			const auto per = [&](double total) { return c.captures ? total / captures : 0.0; };
			logger::info(
				"[Mirrors BENCH] step={} name=\"{}\" rendering={} resolution={} refreshHz={} everyFrame={} roomReuse={} screenSized={} tinySkip={} paneCrop={} frames={} frameMs={:.2f} p50Ms={:.2f} p95Ms={:.2f} p99Ms={:.2f} attempts={} emptyCaptures={} captures={} capturesPerSecond={:.1f} captureMs={:.2f} shadowsMs={:.2f} cullMs={:.2f} drawMs={:.2f} lightingMs={:.2f} otherMs={:.2f} gpuMs={:.2f} gpuSamples={} lightingGpuMs={:.2f} lightingGpuSamples={} mirrorCpuPerFrameMs={:.2f}",
				index + 1, step.name, r.rendering, r.resolution,
				r.refreshHz == MirrorSettingsPolicy::kEveryFrame ? 0u : r.refreshHz,
				r.refreshHz == MirrorSettingsPolicy::kEveryFrame,
				step.roomReuse, step.screenSizedCapture, step.tinyObjectSkip, step.paneCrop,
				r.frames.frames, r.frames.Average(), r.frames.histogram.Percentile(0.50),
				r.frames.histogram.Percentile(0.95), r.frames.histogram.Percentile(0.99),
				c.attempts, c.empty, c.captures, r.seconds > 0.0 ? captures / r.seconds : 0.0,
				per(c.capture), per(c.shadows), per(c.culling), per(c.drawing), per(c.lighting),
				(std::max)(0.0, per(c.capture) - per(c.shadows) - per(c.culling) - per(c.drawing)),
				c.gpuCount ? c.gpu / double(c.gpuCount) : 0.0, c.gpuCount,
				c.lightingGpuCount ? c.lightingGpu / double(c.lightingGpuCount) : 0.0, c.lightingGpuCount,
				c.services ? c.service / double(c.services) : 0.0);
			LogCaptureEvidence("BENCH", index + 1, c.evidence, r.seconds);
			spdlog::default_logger()->flush();
		} catch (...) {
		}
	}

	void AbortBenchmark(const char* reason) noexcept
	{
		if (!benchmarkClock.Running()) return;
		benchmarkClock.Stop();
		RestoreBenchmarkSettings();
		benchmarkAbortReason = reason;
		benchmarkFinished = true;
		benchmarkActive.store(false, std::memory_order_release);
		benchmarkWantsOn.store(true, std::memory_order_release);
		try {
			logger::info("[Mirrors BENCH] stopped ({}); every setting restored", reason ? reason : "unknown");
		} catch (...) {
		}
	}

	void HandleBenchmark(MirrorPerformance::BenchmarkClock::Event event) noexcept
	{
		using Event = MirrorPerformance::BenchmarkClock::Event;
		const auto index = benchmarkClock.Step();
		switch (event) {
		case Event::kApplyStep:
			ApplyBenchmarkStep(MirrorPerformance::kBenchmarkSteps[index]);
			break;
		case Event::kBeginMeasure: {
			benchmarkFrames = {};
			{
				std::lock_guard lock(benchmarkMutex);
				benchmarkCapture = {};
			}
			benchmarkMeasureStart = Now();
			benchmarkMeasuring.store(true, std::memory_order_release);
			break;
		}
		case Event::kEndMeasure: {
			benchmarkMeasuring.store(false, std::memory_order_release);
			auto& r = benchmarkResults[index];
			r.valid = true;
			r.rendering = MirrorToggle::Active();
			r.resolution = MirrorSettings::ResolutionOverride();
			r.refreshHz = MirrorSettings::RefreshRateOverride();
			r.frames = benchmarkFrames;
			r.seconds = Milliseconds(Now() - benchmarkMeasureStart) / 1000.0;
			{
				std::lock_guard lock(benchmarkMutex);
				r.capture = benchmarkCapture;
			}
			LogBenchmarkStep(index, r);
			break;
		}
		case Event::kFinished:
			RestoreBenchmarkSettings();
			benchmarkFinished = true;
			try {
				const auto& first = benchmarkResults[2];
				const auto& last = benchmarkResults.back();
				const bool driftOK = first.valid && last.valid && MirrorPerformance::BenchmarkDriftAcceptable(
					first.frames.Average(), last.frames.Average(), first.frames.histogram.Percentile(.95), last.frames.histogram.Percentile(.95));
				bool bodyOK = true;
				for (const auto& r : benchmarkResults) bodyOK &= r.valid && r.capture.evidence.Comparable(r.rendering);
				benchmarkDriftAcceptable = driftOK;
				benchmarkImageComplete = bodyOK;
				logger::info("[Mirrors BENCH] comparison={} driftAcceptable={} playerEvidenceComplete={} MediumMeanMs={:.3f}->{:.3f} MediumP95Ms={:.1f}->{:.1f} MediumP99Ms={:.1f}->{:.1f}; raw rows retained, no normalized percentile estimates",
					driftOK && bodyOK ? "comparable" : "INCONCLUSIVE", driftOK, bodyOK,
					first.frames.Average(), last.frames.Average(), first.frames.histogram.Percentile(.95), last.frames.histogram.Percentile(.95),
					first.frames.histogram.Percentile(.99), last.frames.histogram.Percentile(.99));
				logger::info("[Mirrors BENCH] finished; every setting restored ({} steps)",
					MirrorPerformance::kBenchmarkSteps.size());
			} catch (...) {
			}
			break;
		default:
			break;
		}
	}

	void RunBenchmark(bool eligible) noexcept
	{
		static std::int64_t last = 0;
		static bool lastEligible = false;
		const auto now = Now();
		const double deltaMs = last ? Milliseconds(now - last) : 0.0;
		const bool countable = eligible && lastEligible && deltaMs > 0.0 && deltaMs < 5000.0;
		last = now;
		lastEligible = eligible;
		if (benchmarkRequested.exchange(false, std::memory_order_acq_rel)) {
			if (benchmarkClock.Running()) {
				AbortBenchmark("F8 pressed");
			} else if (!REL::Module::IsVR() && MirrorSettings::DebugKeysEnabled()) {
				SaveOptimizations();
				benchmarkResults = {};
				benchmarkFinished = false;
				benchmarkDriftAcceptable = benchmarkImageComplete = false;
				benchmarkAbortReason = nullptr;
				benchmarkShowing = true;
				benchmarkClock.Start();
				benchmarkActive.store(true, std::memory_order_release);
				benchmarkWantsOn.store(true, std::memory_order_release);
				try {
					logger::info("[Mirrors BENCH] started: {} steps, about {} s; stand still facing a mirror",
						MirrorPerformance::kBenchmarkSteps.size(),
						MirrorPerformance::BenchmarkClock::TotalMicroseconds() / 1'000'000u);
				} catch (...) {
				}
			}
		}
		if (!benchmarkClock.Running()) return;
		if (!MirrorSettings::DebugKeysEnabled()) {
			AbortBenchmark("debug keys switched off");
			return;
		}
		if (countable && benchmarkMeasuring.load(std::memory_order_relaxed))
			benchmarkFrames.Add(deltaMs);
		auto event = benchmarkClock.Advance(countable ? static_cast<std::uint64_t>(deltaMs * 1000.0) : 0u, eligible);
		for (unsigned guard = 0; event != MirrorPerformance::BenchmarkClock::Event::kNone && guard < 8u; ++guard) {
			HandleBenchmark(event);
			event = benchmarkClock.Advance(0u, eligible);
		}
		benchmarkActive.store(benchmarkClock.Running(), std::memory_order_release);
		if (!benchmarkClock.Running())
			benchmarkWantsOn.store(true, std::memory_order_release);
	}
}

namespace MirrorPerformance
{
	void SetEnabled(bool enabled) noexcept
	{
		const auto previous = phase.load(std::memory_order_acquire);
		if (previous && ((previous & 1u) != 0u) == enabled) return;
		const auto next = (((previous >> 1u) + 1u) << 1u) | (enabled ? 1u : 0u);
		{
			std::lock_guard lock(animationMutex);
			animationTicks = animationCount = 0;
			workerTotals = {};
			phase.store(next, std::memory_order_release);
		}
		totals = {}; publications = {}; captures = {}; collections = {};
		captureEvidence = {};
		shadowRenders = {}; shadowReuses = {};
		gpu.DiscardResults();
		phaseStartedMs = previousReportMs = GetTickCount64();
		logger::info("[Mirrors AB] phase={} enabled={}; exclude five seconds after switching or resuming; keep the same view and settings for comparison; installed hooks remain resident",
			next >> 1u, enabled ? "ON" : "OFF");
	}

	void ObserveFrame(PhaseFrameTiming& timing, bool worldActive, const char* source, const char* description) noexcept
	{
		const auto current = phase.load(std::memory_order_acquire);
		const auto resolution = MirrorSettings::ResolutionOverride(), refreshHz = MirrorSettings::RefreshRateOverride();
		const auto configuration = (std::uint64_t{ resolution } << 32u) | refreshHz;
		if (!timing.Observe(Milliseconds(Now()), worldActive, current, configuration))
			return;
		const auto& frames = timing.frames;
		logger::info("[Mirrors PERF] {}: enabled={} phase={} settled=true FPS={:.2f} frameAvg={:.3f}ms p50={:.1f}ms p95={:.1f}ms p99={:.1f}ms frameMax={:.3f}ms over33ms={} over50ms={} frames={} window={:.2f}s resolution={} refreshHz={}; everyFrame={}; {}",
			source, (current & 1u) ? "ON" : "OFF", current >> 1u,
			1000.0 * frames.count / frames.totalMs, frames.totalMs / frames.count,
			frames.histogram.Percentile(0.50), frames.histogram.Percentile(0.95), frames.histogram.Percentile(0.99),
			frames.maximumMs, frames.over33ms, frames.over50ms, frames.count, frames.totalMs / 1000.0,
			resolution, refreshHz == MirrorSettingsPolicy::kEveryFrame ? 0u : refreshHz,
			refreshHz == MirrorSettingsPolicy::kEveryFrame, description);
		timing.frames.NextWindow();
	}
	void FramePresented(bool worldActive) noexcept
	{
		presentedFrames.fetch_add(1u, std::memory_order_relaxed);
		RunBenchmark(worldActive);
		if (!REL::Module::IsVR())
			MirrorSettings::ObserveQualityFrame(Milliseconds(Now()), worldActive && MirrorToggle::Active(),
				overlayPublications.load(std::memory_order_relaxed), qualitySceneCaptures.load(std::memory_order_relaxed), qualitySceneNodes.load(std::memory_order_relaxed), qualityBodyFaults.load(std::memory_order_relaxed));
		
		overlayMetrics.Observe(Milliseconds(Now()), worldActive, phase.load(std::memory_order_acquire),
			overlayPublications.load(std::memory_order_relaxed), MirrorToggle::Active());
		static PhaseFrameTiming timing;
		ObserveFrame(timing, worldActive, "application Present",
			REL::Module::IsVR() ? "desktop swap chain, not headset FPS" : "world rendering, includes Present wait");
	}
	OverlaySnapshot GetOverlaySnapshot() noexcept { return overlayMetrics.Snapshot(); }
	void FrameRendered(bool worldActive) noexcept
	{
		if (REL::Module::IsVR())
			MirrorSettings::ObserveQualityFrame(Milliseconds(Now()), worldActive && MirrorToggle::Active(),
				overlayPublications.load(std::memory_order_relaxed), qualitySceneCaptures.load(std::memory_order_relaxed), qualitySceneNodes.load(std::memory_order_relaxed), qualityBodyFaults.load(std::memory_order_relaxed));
		static PhaseFrameTiming timing;
		ObserveFrame(timing, worldActive, "application render loop", "VR world rendering, not headset/compositor FPS");
	}

	Sample::Sample(Stage stage, unsigned slot, ID3D11DeviceContext* context) noexcept : start_(Now()), phase_(phase.load(std::memory_order_acquire)), tag_(static_cast<unsigned>(stage) * 3 + (std::min)(slot, 2u))
	{

		const bool composite = stage == Stage::FlatComposite || stage == Stage::VRComposite;
		const bool lightingPass = stage == Stage::FlatResolve;
		gpu_ = -2;  

		if (context && context != globals::d3d::context) {
			gpu_ = -1;
			return;
		}
		const bool cpuOnly = stage >= Stage::FlatService && !lightingPass;
		const auto sampleSlot = sampleFrame % 4u;
		const bool sampleWanted = composite ? sampleSlot == 0u : (lightingPass ? sampleSlot == 1u : sampleSlot >= 2u);
		if (!cpuOnly && !gpuSampleTaken && sampleWanted) {
			gpuSampleTaken = true;
			gpu_ = gpu.Begin(globals::d3d::device, globals::d3d::context, tag_);
		}
	}
	Sample::~Sample() { Finish(); }
	void Sample::Finish(bool completed) noexcept
	{
		if (!active_)
			return;
		active_ = false;
		gpu.End(globals::d3d::context, gpu_);
		if (phase_ != phase.load(std::memory_order_acquire)) return;
		auto& value = totals[tag_];
		const double elapsed = Milliseconds(Now() - start_);
		++value.attempts;
		value.completed += completed ? 1u : 0u;
		value.gpuUnavailable += gpu_ == -1 ? 1u : 0u;
		value.cpuSum += elapsed;
		value.cpuMax = (std::max)(value.cpuMax, elapsed);
		switch (static_cast<Stage>(tag_ / 3u)) {
		case Stage::FlatNative:
		case Stage::FlatMaterial:
			++live.captures; live.capture += elapsed;
			AddBenchmark([&](BenchmarkCapture& b) { ++b.captures; b.capture += elapsed; });
			break;
		case Stage::FlatPrivateSun:
			live.shadows += elapsed;
			AddBenchmark([&](BenchmarkCapture& b) { b.shadows += elapsed; });
			break;
		case Stage::FlatResolve:
			live.lighting += elapsed;
			AddBenchmark([&](BenchmarkCapture& b) { b.lighting += elapsed; });
			break;
		case Stage::FlatService:
			++live.services; live.service += elapsed;
			AddBenchmark([&](BenchmarkCapture& b) { ++b.services; b.service += elapsed; });
			break;
		case Stage::FlatComposite: ++live.composites; live.composite += elapsed; break;
		default: break;
		}
	}
	AnimationSample::AnimationSample() noexcept : start_(Now()), phase_(phase.load(std::memory_order_acquire)) {}
	AnimationSample::~AnimationSample()
	{
		const auto elapsed = static_cast<std::uint64_t>(Now() - start_);
		std::lock_guard lock(animationMutex);
		if (phase_ != phase.load(std::memory_order_acquire)) return;
		animationTicks += elapsed;
		++animationCount;
	}
	WorkerSample::WorkerSample(Stage stage) noexcept : start_(Now()), phase_(phase.load(std::memory_order_acquire)), tag_(static_cast<unsigned>(stage) * 3u) {}
	WorkerSample::~WorkerSample()
	{
		const double elapsed = Milliseconds(Now() - start_);
		std::lock_guard lock(animationMutex);
		if (phase_ != phase.load(std::memory_order_acquire) || tag_ >= workerTotals.size()) return;
		auto& value = workerTotals[tag_];
		++value.attempts; ++value.completed;
		value.cpuSum += elapsed;
		value.cpuMax = (std::max)(value.cpuMax, elapsed);
	}
	void Published(unsigned slot, std::uint64_t count) noexcept
	{
		if (slot < publications.size()) {
			publications[slot] += count;
			overlayPublications.fetch_add(count, std::memory_order_relaxed);
		}
	}
	void PublishedReceiver(unsigned id, unsigned width, unsigned height, unsigned frame,
		std::uint64_t pose, bool playerRequired, bool playerSubmitted) noexcept
	{
		const auto now = Milliseconds(Now());
		captureEvidence.Publish(id,width,height,frame,pose,playerRequired,playerSubmitted,now);
		AddBenchmark([&](BenchmarkCapture& b) { b.evidence.Publish(id,width,height,frame,pose,playerRequired,playerSubmitted,now); });
	}
	void SourceFrame(unsigned frame, bool worldActive) noexcept
	{
		const auto now = Milliseconds(Now());
		captureEvidence.SourceFrame(frame,now,worldActive);
		AddBenchmark([&](BenchmarkCapture& b) { b.evidence.SourceFrame(frame,now,worldActive); });
	}
	void StorageAllocation(unsigned width, unsigned height, double ms, bool success, bool pooled, std::uint64_t retainedBytes) noexcept
	{
		captureEvidence.Allocation(width,height,ms,success,pooled,retainedBytes);
		AddBenchmark([&](BenchmarkCapture& b) { b.evidence.Allocation(width,height,ms,success,pooled,retainedBytes); });
	}
	void CaptureResult(unsigned slot, bool returned, unsigned faces, bool clipped, bool playerUnavailable, double cullMs, double drawMs) noexcept
	{
		if (slot >= captures.size()) return;
		auto& value = captures[slot];
		++value.attempts;
		value.faults += !returned;
		value.zeroDraw += returned && !faces;
		value.unclipped += returned && faces && !clipped;
		value.playerUnavailable += playerUnavailable;
		if (playerUnavailable) {
			qualityBodyFaults.fetch_add(1, std::memory_order_relaxed);
			++captureEvidence.bodyMissing;
		}
		value.cullSum += cullMs; value.cullMax = (std::max)(value.cullMax, cullMs);
		value.drawSum += drawMs; value.drawMax = (std::max)(value.drawMax, drawMs);
		live.culling += cullMs;
		live.drawing += drawMs;
		AddBenchmark([&](BenchmarkCapture& b) {
			++b.attempts;
			b.empty += !returned || !faces;
			b.evidence.bodyMissing += playerUnavailable;
			b.culling += cullMs;
			b.drawing += drawMs;
		});
	}
	void SceneCollected(unsigned slot, const SceneCollectionCounts& counts, std::uint32_t retained) noexcept
	{

		qualitySceneNodes.fetch_add(std::uint64_t{retained} + counts.tinyRejected, std::memory_order_relaxed);
		qualitySceneCaptures.fetch_add(1, std::memory_order_relaxed);
		if (slot >= collections.size()) return;
		auto& value = collections[slot];
		++value.count;
		value.visited += counts.visited; value.boundsTested += counts.boundsTested;
		value.rejected += counts.rejected; value.partitioned += counts.partitioned;
		value.rangeRejected += counts.rangeRejected; value.distantRoots += counts.distantRoots;
		value.tinyRejected += counts.tinyRejected;
		value.retained += retained;
	}
	
	void PublishBreakdown() noexcept
	{
		const auto nowTicks = Now();
		if (!liveStart) liveStart = nowTicks;
		const double windowMs = Milliseconds(nowTicks - liveStart);
		if (windowMs < 1000.0) return;
		Breakdown next{};
		next.valid = true;
		next.detailed = detailedTiming.load(std::memory_order_relaxed);
		next.seconds = windowMs / 1000.0;
		const double captures = static_cast<double>(live.captures);
		next.capturesPerSecond = captures / next.seconds;
		const auto ownTicks = ownCaptureTicks.exchange(0, std::memory_order_relaxed);
		const auto ownCommits = ownCaptureCommits.exchange(0u, std::memory_order_relaxed);
		if (live.captures) {
			next.capture = live.capture / captures;
			next.shadows = live.shadows / captures;
			next.culling = live.culling / captures;
			next.drawing = live.drawing / captures;
			next.other = (std::max)(0.0, next.capture - next.shadows - next.culling - next.drawing);
			next.ownDrawWork = Milliseconds(ownTicks) / captures;
			next.drawCommits = static_cast<double>(ownCommits) / captures;
			next.lighting = live.lighting / captures;
		}
		next.gpu = live.gpuCount ? live.gpu / static_cast<double>(live.gpuCount) : -1.0;
		next.lightingGpu = live.lightingGpuCount ? live.lightingGpu / static_cast<double>(live.lightingGpuCount) : -1.0;
		next.servicePerFrame = live.services ? live.service / static_cast<double>(live.services) : 0.0;
		next.compositePerFrame = live.composites ? live.composite / static_cast<double>(live.composites) : 0.0;
		const auto frames = presentedFrames.exchange(0u, std::memory_order_relaxed);
		const auto viewTicks = ownViewTicks.exchange(0, std::memory_order_relaxed);
		next.mainViewOwnWorkPerFrame = frames ? Milliseconds(viewTicks) / static_cast<double>(frames) : 0.0;
		const auto observed = observedFrames.exchange(0u, std::memory_order_relaxed);
		const auto moving = movingFrames.exchange(0u, std::memory_order_relaxed);
		next.movingShare = observed ? static_cast<double>(moving) / static_cast<double>(observed) : -1.0;
		{
			std::lock_guard lock(breakdownMutex);
			breakdown = next;
		}
		live = {};
		liveStart = nowTicks;
	}
	Breakdown GetBreakdown() noexcept
	{
		std::lock_guard lock(breakdownMutex);
		return breakdown;
	}
	void SetDetailedTiming(bool enabled) noexcept { detailedTiming.store(enabled, std::memory_order_relaxed); }
	bool DetailedTimingEnabled() noexcept { return detailedTiming.load(std::memory_order_relaxed); }
	std::int64_t Ticks() noexcept { return Now(); }
	void OwnDrawWork(bool capture, std::int64_t ticks) noexcept
	{
		if (ticks <= 0) return;
		if (capture) {
			ownCaptureTicks.fetch_add(ticks, std::memory_order_relaxed);
			ownCaptureCommits.fetch_add(1u, std::memory_order_relaxed);
		} else {
			ownViewTicks.fetch_add(ticks, std::memory_order_relaxed);
		}
	}
	void CameraMoved(bool moving) noexcept
	{
		cameraMoving.store(moving, std::memory_order_relaxed);
		observedFrames.fetch_add(1u, std::memory_order_relaxed);
		if (moving) movingFrames.fetch_add(1u, std::memory_order_relaxed);
	}
	bool BenchmarkActive() noexcept { return benchmarkActive.load(std::memory_order_acquire); }
	void RequestBenchmark() noexcept { benchmarkRequested.store(true, std::memory_order_release); }
	bool BenchmarkShowing() noexcept { return benchmarkShowing; }
	void DismissBenchmark() noexcept
	{
		if (benchmarkClock.Running()) return;
		benchmarkShowing = false;
	}
	std::size_t BenchmarkLines(BenchmarkLine* lines, std::size_t capacity) noexcept
	{
		if (!lines || capacity < 4) return 0;
		std::size_t n = 0;
		const auto put = [&](const char* format, auto... arguments) {
			if (n < capacity) std::snprintf(lines[n++].data(), lines[0].size(), format, arguments...);
		};
		put("BENCHMARK (F8 starts or stops, F11 hides the table)");
		const auto seconds = static_cast<unsigned>((benchmarkClock.Remaining() + 999'999u) / 1'000'000u);
		if (benchmarkClock.Running()) {
			const auto index = benchmarkClock.Step();
			const auto& name = kBenchmarkSteps[index].name;
			if (benchmarkClock.CountingDown())
				put("Starting in %u s: stand still facing a mirror", seconds);
			else
				put("Step %u/%u %.*s: %s, %u s", static_cast<unsigned>(index + 1),
					static_cast<unsigned>(kBenchmarkSteps.size()), static_cast<int>(name.size()), name.data(),
					benchmarkClock.Measuring() ? "measuring" : "settling", seconds);
		} else if (benchmarkAbortReason) {
			put("Stopped: %s (settings restored)", benchmarkAbortReason);
		} else if (benchmarkFinished) {
			if (!benchmarkImageComplete)
				put("INCONCLUSIVE: incomplete reflection/body evidence; settings restored");
			else if (!benchmarkDriftAcceptable)
				put("INCONCLUSIVE: baseline drift; raw results below; settings restored");
			else
				put("Done - settings restored; the full table is in the log");
		}
		put("%-28s %6s %6s %6s | %6s %6s %5s", "Setting", "frame", "p95", "p99", "cpu", "gpu", "empty");
		for (std::size_t i = 0; i < benchmarkResults.size(); ++i) {
			const auto& r = benchmarkResults[i];
			if (!r.valid) continue;
			const auto& name = kBenchmarkSteps[i].name;
			const auto& c = r.capture;
			
			if (c.captures)
				put("%-28.*s %6.2f %6.2f %6.2f | %6.2f %6.2f %4u%%", static_cast<int>(name.size()), name.data(),
					r.frames.Average(), r.frames.histogram.Percentile(0.95), r.frames.histogram.Percentile(0.99),
					c.capture / double(c.captures), c.gpuCount ? c.gpu / double(c.gpuCount) : 0.0,
					c.attempts ? static_cast<unsigned>((100 * c.empty + c.attempts / 2) / c.attempts) : 0u);
			else
				put("%-28.*s %6.2f %6.2f %6.2f | %6s %6s", static_cast<int>(name.size()), name.data(),
					r.frames.Average(), r.frames.histogram.Percentile(0.95), r.frames.histogram.Percentile(0.99), "-", "-");
		}
		return n;
	}
	bool BenchmarkWantsMirrorsOn() noexcept { return benchmarkWantsOn.load(std::memory_order_acquire); }
	void Tick() noexcept
	{
		MirrorSettings::ApplyAutomaticQuality();
		++sampleFrame;
		gpuSampleTaken = false;
		gpu.Poll(globals::d3d::context, [](const MirrorGpuTimer::Result& result) noexcept {
			if (result.tag >= totals.size())
				return;
			auto& value = totals[result.tag];
			if (result.valid) {
				++value.gpuCount;
				value.gpuSum += result.milliseconds;
				if (IsCaptureTag(result.tag)) {
					++live.gpuCount;
					live.gpu += result.milliseconds;
					AddBenchmark([&](BenchmarkCapture& b) { ++b.gpuCount; b.gpu += result.milliseconds; });
				} else if (static_cast<Stage>(result.tag / 3u) == Stage::FlatResolve) {
					++live.lightingGpuCount;
					live.lightingGpu += result.milliseconds;
					AddBenchmark([&](BenchmarkCapture& b) { ++b.lightingGpuCount; b.lightingGpu += result.milliseconds; });
				}
			} else {
				++value.gpuUnavailable;
			}
		});
		PublishBreakdown();
		const auto now = GetTickCount64();
		if (!previousReportMs) previousReportMs = now;
		if (now - previousReportMs < 5000)
			return;
		const double seconds = static_cast<double>(now - previousReportMs) / 1000.0;
		const bool settled = previousReportMs >= phaseStartedMs + 5000u;
		previousReportMs = now;
		const auto current = phase.load(std::memory_order_acquire);
		const auto* enabled = (current & 1u) ? "ON" : "OFF";
		constexpr const char* names[]{ "flat-native", "flat-material", "vr-stereo", "flat-composite", "vr-composite", "flat-service", "vr-service", "flat-shadow-copy", "flat-lighting-prepare", "flat-private-sun", "flat-sun-collect", "flat-sun-draw", "flat-sun-arm", "flat-sun-submit", "flat-sun-cull", "flat-sun-cache", "flat-sun-accumulate", "flat-sun-raster", "flat-sun-retire", "flat-resolve" };
		static_assert(std::size(names) == static_cast<unsigned>(Stage::Count));
		std::uint64_t ticks{}, count{};
		{
			std::lock_guard lock(animationMutex);
			ticks = std::exchange(animationTicks, 0); count = std::exchange(animationCount, 0);
			for (unsigned i = 0; i < totals.size(); ++i) {
				totals[i].attempts += workerTotals[i].attempts;
				totals[i].completed += workerTotals[i].completed;
				totals[i].cpuSum += workerTotals[i].cpuSum;
				totals[i].cpuMax = (std::max)(totals[i].cpuMax, workerTotals[i].cpuMax);
			}
			workerTotals = {};
		}
		for (unsigned i = 0; i < totals.size(); ++i) {
			auto& value = totals[i];
			if (value.attempts || value.gpuCount || value.gpuUnavailable) {
				const auto stage = static_cast<Stage>(i / 3u);
				const bool cpuOnly = stage >= Stage::FlatService && stage != Stage::FlatResolve;
				const auto gpuText = cpuOnly ? std::string("not measured for service total") :
					(value.gpuCount ? fmt::format("{:.3f}ms", value.gpuSum / value.gpuCount) : std::string("unavailable"));
				logger::info("[Mirrors PERF] stage={} slot={} attempts={} completed={} CPUavg={:.3f}ms CPUmax={:.3f}ms GPUavg={} GPU samples={} unavailable={} window={:.2f}s enabled={} phase={} settled={}",
					names[i / 3], i % 3, value.attempts, value.completed,
					value.attempts ? value.cpuSum / value.attempts : 0.0, value.cpuMax,
					gpuText, value.gpuCount, value.gpuUnavailable, seconds, enabled, current >> 1u, settled);
			}
			value = {};
		}
		for (unsigned i = 0; i < captures.size(); ++i) {
			const auto& value = captures[i];
			if (!value.attempts) continue;
			logger::info("[Mirrors PERF] capture results slot={} attempts={} faults={} zeroDraw={} playerUnavailable={} unclipped={} cullAvg={:.3f}ms cullMax={:.3f}ms drawAvg={:.3f}ms drawMax={:.3f}ms window={:.2f}s enabled={} phase={}; completed means faces rendered, publication is counted at commit",
				i, value.attempts, value.faults, value.zeroDraw, value.playerUnavailable, value.unclipped,
				value.cullSum / value.attempts, value.cullMax, value.drawSum / value.attempts, value.drawMax, seconds, enabled, current >> 1u);
		}
		captures = {};
		for (unsigned i = 0; i < collections.size(); ++i) {
			const auto& value = collections[i];
			if (!value.count) continue;
			const double divisor = static_cast<double>(value.count);
			logger::info("[Mirrors PERF] scene collection slot={} captures={} visitedAvg={:.1f} boundTestsAvg={:.1f} rejectedAvg={:.1f} splitGroupsAvg={:.1f} retainedAvg={:.1f} rangeRejectedAvg={:.1f} distantRootsAvg={:.1f} tinyRejectedAvg={:.1f} window={:.2f}s enabled={} phase={} settled={}; entries are subtrees or leaves, not draw calls; sunlight collection excluded",
				i, value.count, value.visited / divisor, value.boundsTested / divisor,
				value.rejected / divisor, value.partitioned / divisor, value.retained / divisor,
				value.rangeRejected / divisor, value.distantRoots / divisor, value.tinyRejected / divisor,
				seconds, enabled, current >> 1u, settled);
		}
		collections = {};
		if (shadowRenders[0] || shadowRenders[1] || shadowRenders[2] || shadowReuses[0] || shadowReuses[1]) {
			logger::info("[Mirrors PERF] private-sun maps wide(rendered/reused)={}/{} detail(rendered/reused)={}/{} playerMasks={} window={:.2f}s enabled={} phase={} settled={}; reuse is within one source frame and pose",
				shadowRenders[0], shadowReuses[0], shadowRenders[1], shadowReuses[1], shadowRenders[2],
				seconds, enabled, current >> 1u, settled);
		}
		shadowRenders = {}; shadowReuses = {};
		logger::info("[Mirrors PERF] accepted publications/s by capture worker={:.2f}/{:.2f}/{:.2f} private animation updates={} CPUtotal={:.3f}ms window={:.2f}s enabled={} phase={} settled={}; commit events across all receivers; GPU samples arrive asynchronously; game cadence is logged separately",
			publications[0] / seconds, publications[1] / seconds, publications[2] / seconds,
			count, Milliseconds(static_cast<std::int64_t>(ticks)), seconds, enabled, current >> 1u, settled);
		publications = {};
		LogCaptureEvidence("PERF", 0, captureEvidence, seconds);
		captureEvidence = {};
	}
	void ShadowMap(unsigned map, bool reused) noexcept
	{
		if (map >= shadowRenders.size() || !(phase.load(std::memory_order_relaxed) & 1u)) return;
		if (reused) ++shadowReuses[map];
		else ++shadowRenders[map];
	}
}
