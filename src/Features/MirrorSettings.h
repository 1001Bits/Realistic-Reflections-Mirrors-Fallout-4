#pragma once

#include <cstdint>
#include "MirrorSettingsMenu.h"

namespace MirrorSettings
{
	void Initialize();
	void Reload();
	bool ReadMenuValues(MirrorSettingsMenu::Values& values);
	bool SaveMenuValues(const MirrorSettingsMenu::Values& values, std::uint32_t changed);
	std::uint64_t MenuRevision() noexcept;
	MirrorSettingsMenu::Synchronization MenuChanges();
	
	int QualityModeForMenu() noexcept;
	void SelectQualityModeForMenu(int mode) noexcept;
	
	void ObserveQualityFrame(double nowMs, bool worldActive, std::uint64_t publications,
		std::uint64_t sceneCaptures = 0, std::uint64_t sceneNodes = 0, std::uint64_t bodyFaults = 0) noexcept;
	void ApplyAutomaticQuality() noexcept;
	bool AutomaticQualityAdjustment() noexcept;
	bool SpatialCadenceEnabled() noexcept;
	unsigned AutomaticQualityLevel() noexcept;
	
	void CaptureResourcesChanged() noexcept;
	bool CanPrepareCaptureResources() noexcept;
	std::uint32_t ResolutionOverride() noexcept;
	
	std::uint32_t RefreshRateOverride() noexcept;
	bool ShadowsEnabled() noexcept;
	
	bool ShadowRenderingEnabled() noexcept;
	bool PlayerSunlightShadowsEnabled() noexcept;
	
	bool PlayerShadowsEnabled() noexcept;
	
	bool DebugKeysEnabled() noexcept;
	bool SmallObjectShadowsEnabled() noexcept;
	bool SunlightShadowsOutdoorsOnly() noexcept;
	
	std::uint32_t OutdoorDistance() noexcept;

	std::uint32_t ShadowDistance() noexcept;
	
	std::uint32_t ShadowResolution() noexcept;
	std::uint32_t ShadowDetailResolution() noexcept;
	bool LowShadowQuality() noexcept;
	
	std::uint32_t MinimumSunLight() noexcept;
	std::uint32_t CaptureSize(std::uint32_t authored) noexcept;
	
	std::uint32_t LightBudget() noexcept;
	std::uint64_t CaptureInterval(std::uint64_t fallback = 16667u) noexcept;
	std::uint64_t PublicationLifetime() noexcept;

	struct TemporaryValues
	{
		std::uint32_t resolution{}, refreshRate{}, objectDistance{}, shadowDistance{};
		bool shadows{ true };
		bool lowShadowQuality{};
	};
	void SetTemporaryValues(const TemporaryValues* values) noexcept;
	bool TemporaryValuesActive() noexcept;
}
