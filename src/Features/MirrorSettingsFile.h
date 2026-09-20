#pragma once

#include "MirrorSettingsPolicy.h"
#include "MirrorQualityPreset.h"
#include "MirrorQualityMode.h"
#include "MirrorSceneRange.h"
#include "MirrorAdaptiveQuality.h"
#include "MirrorSettingsMenu.h"
#include <cwchar>
#include <filesystem>
#include <vector>
#include <windows.h>

namespace MirrorSettingsFile
{
	struct Paths
	{
		std::filesystem::path defaults;
		std::filesystem::path user;
	};
	struct Values
	{
		std::uint32_t resolution{};
		std::uint32_t refreshRate{};
		bool shadows{true};
		bool smallObjectShadows{false};
		bool sunlightShadowsOutdoorsOnly{false};
		bool limitedOutdoorRange{true};
		std::uint32_t outdoorDistance{MirrorSceneRange::DefaultDistance};
		bool playerSunlightShadows{true};
		std::uint32_t lightBudget{MirrorSettingsPolicy::kDefaultLightBudget};
		std::uint32_t shadowDistance{MirrorSceneRange::DefaultDistance};

		std::uint32_t shadowResolution{2048}, shadowDetailResolution{2048};
		std::uint32_t minimumSunLight{15};
		
		bool playerShadows{true};

		bool automaticQuality{true};
		bool automaticQualityAdjustment{false};
		MirrorQualityMode::Mode qualityMode{MirrorQualityMode::kDefault};
		unsigned automaticTargetFPS{MirrorAdaptiveQuality::kDefaultTargetFPS};
		MirrorQualityPreset::Level qualityLevel{MirrorQualityPreset::Level::kMedium};

		bool debugKeys{false};
		bool operator==(const Values& other) const
		{
			return automaticQualityAdjustment == other.automaticQualityAdjustment &&
				automaticTargetFPS == other.automaticTargetFPS &&
				resolution == other.resolution && refreshRate == other.refreshRate &&
				shadows == other.shadows && smallObjectShadows == other.smallObjectShadows &&
				sunlightShadowsOutdoorsOnly == other.sunlightShadowsOutdoorsOnly &&
				limitedOutdoorRange == other.limitedOutdoorRange && outdoorDistance == other.outdoorDistance &&
				playerSunlightShadows == other.playerSunlightShadows && lightBudget == other.lightBudget &&
				shadowDistance == other.shadowDistance && shadowResolution == other.shadowResolution &&
				shadowDetailResolution == other.shadowDetailResolution && minimumSunLight == other.minimumSunLight &&
				playerShadows == other.playerShadows;
		}
	};

	inline int ReadInt(const Paths& paths, const wchar_t* key, int fallback)
	{
		
		const auto original = static_cast<int>(GetPrivateProfileIntW(L"Mirrors", key, fallback, paths.defaults.c_str()));
		return static_cast<int>(GetPrivateProfileIntW(L"Mirrors", key, original, paths.user.c_str()));
	}

	inline bool HasKey(const std::filesystem::path& path, const wchar_t* key)
	{
		wchar_t value[2]{};
		return GetPrivateProfileStringW(L"Mirrors", key, L"", value, 2, path.c_str()) != 0;
	}

	inline MirrorQualityMode::Mode ReadMode(const Paths& paths)
	{
		const auto legacy = MirrorQualityMode::Legacy(
			ReadInt(paths, L"bAutomaticQualityAdjustment", 0) > 0,
			ReadInt(paths, L"bAutomaticQuality", 1) > 0);
		const auto read = [](const std::filesystem::path& path) {
			wchar_t text[32]{};
			GetPrivateProfileStringW(L"Mirrors", L"iQualityMode", L"", text, 32, path.c_str());
			wchar_t* end{};
			const auto value = std::wcstol(text, &end, 10);
			return end != text && *end == L'\0' && value >= 0 && value <= 2 ?
				MirrorQualityMode::Clamp(static_cast<int>(value)) : MirrorQualityMode::kDefault;
		};
		if (HasKey(paths.user, L"iQualityMode")) return read(paths.user);
		
		if (HasKey(paths.user, L"bAutomaticQualityAdjustment") || HasKey(paths.user, L"bAutomaticQuality"))
			return legacy;
		return HasKey(paths.defaults, L"iQualityMode") ? read(paths.defaults) : legacy;
	}

	inline MirrorSettingsMenu::Values ReadMenu(const Paths& paths)
	{
		using namespace MirrorSettingsMenu;
		MirrorSettingsMenu::Values result;
		for (unsigned i = 0; i < Count; ++i) {
			const auto& control = controls[i];
			int value = ReadInt(paths, control.key, control.fallback);
			if (control.Boolean()) value = value > 0;
			else if ((control.kind == Kind::Choice && (value < control.minimum || value > control.maximum)) ||
				(i == ShadowResolution && (value < 512 || value > 4096 || value % 512 != 0))) value = control.fallback;
			result[i] = std::clamp(value, control.minimum, control.maximum);
		}
		result[Mode] = static_cast<int>(ReadMode(paths));
		if (result[Mode] != 0) result[PresetBaseline] = result[Mode] == 1;
		return result;
	}

	inline bool WriteMenu(const Paths& paths, const MirrorSettingsMenu::Values& values, std::uint32_t changed)
	{
		using namespace MirrorSettingsMenu;
		if (!changed) return true;
		if (changed & (1u << Mode)) changed |= 1u << PresetBaseline;
		if (paths.user.empty() || (changed >> Count)) return false;
		// Merge only edited keys into the current user section. Defaults, hidden
		// preferences, unrelated keys and other sections are not replaced by UI state.
		std::vector<wchar_t> buffer(32768);
		const auto size = GetPrivateProfileSectionW(L"Mirrors", buffer.data(),
			static_cast<DWORD>(buffer.size()), paths.user.c_str());
		if (size >= buffer.size() - 2) return false;
		std::vector<std::wstring> entries;
		for (const wchar_t* item = buffer.data(); *item; item += std::wcslen(item) + 1) entries.emplace_back(item);
		for (unsigned i = 0; i < Count; ++i) {
			if (!(changed & (1u << i))) continue;
			const auto& control = controls[i];
			if (values[i] < control.minimum || values[i] > control.maximum ||
				(control.step > 1 && (values[i] - control.minimum) % control.step != 0)) return false;
			const std::wstring replacement = std::wstring(control.key) + L"=" + std::to_wstring(values[i]);
			bool found = false;
			for (auto& item : entries) {
				const auto equals = item.find(L'=');
				if (equals != std::wstring::npos && _wcsicmp(item.substr(0, equals).c_str(), control.key) == 0) {
					item = replacement;
					found = true;
				}
			}
			if (!found) entries.push_back(replacement);
		}
		std::wstring section;
		for (const auto& item : entries) { section += item; section += L'\0'; }
		section += L'\0';
		if (section.size() >= buffer.size() - 2) return false;
		std::error_code error;
		std::filesystem::create_directories(paths.user.parent_path(), error);
		return !error && WritePrivateProfileSectionW(L"Mirrors", section.c_str(), paths.user.c_str()) != FALSE;
	}

	inline Values Read(const Paths& paths)
	{
		const auto choice = ReadInt(paths, L"iResolution", 0);
		const auto resolution = choice >= 1 && choice <= 4 ? MirrorSettingsPolicy::kResolutions[choice - 1] : 0u;
		const auto hz = static_cast<std::uint32_t>(std::clamp(ReadInt(paths, L"iRefreshRate", 60),
			5, static_cast<int>(MirrorSettingsPolicy::kMaximumRefreshRate)));
		const auto refresh = ReadInt(paths, L"bEveryFrameRefresh", 0) > 0 ? MirrorSettingsPolicy::kEveryFrame : hz;

		Values values{ resolution, refresh,
			ReadInt(paths, L"bShadows", 1) > 0, ReadInt(paths, L"bSmallObjectShadows", 0) > 0,
			ReadInt(paths, L"bSunlightShadowsOutdoorsOnly", 0) > 0,
			ReadInt(paths, L"bLimitedOutdoorRange", 1) > 0,
			MirrorSceneRange::ClampDistance(ReadInt(paths, L"iOutdoorDistance", MirrorSceneRange::DefaultDistance)),
			true,
			MirrorSettingsPolicy::LightBudgetFromChoice(
				ReadInt(paths, L"iLightBudget", MirrorSettingsPolicy::kDefaultLightBudgetChoice)) };
		
		values.shadowDistance = MirrorSceneRange::ClampDistance(
			ReadInt(paths, L"iShadowDistance", MirrorSceneRange::DefaultDistance));

		const auto validResolution = [](int value, std::uint32_t fallback) {
			return value >= 512 && value <= 4096 && value % 512 == 0 ?
				static_cast<std::uint32_t>(value) : fallback;
		};
		values.shadowResolution = validResolution(ReadInt(paths, L"iShadowResolution", 2048), 2048u);

		values.shadowDetailResolution = values.shadowResolution;
		values.minimumSunLight = static_cast<std::uint32_t>(
			std::clamp(ReadInt(paths, L"iMinimumSunLight", 15), 0, 100));
		values.playerShadows = true;
		values.debugKeys = ReadInt(paths, L"bDebugKeys", 0) > 0;
		values.qualityMode = ReadMode(paths);

		values.automaticQuality = MirrorQualityMode::UsesPreset(values.qualityMode,
			ReadInt(paths, L"bAutomaticQuality", 1) > 0);
		values.automaticQualityAdjustment = values.qualityMode == MirrorQualityMode::Mode::Automatic;
		values.automaticTargetFPS = MirrorAdaptiveQuality::TargetFPS(ReadInt(paths, L"iAutomaticTargetFPS", 60));
		values.qualityLevel = MirrorQualityPreset::Clamp(ReadInt(paths, L"iQualityLevel",
			static_cast<int>(MirrorQualityPreset::Level::kMedium)));
		if (values.automaticQuality) {
			const auto chosen = MirrorQualityPreset::For(values.qualityLevel);
			values.resolution = chosen.resolution;
			values.refreshRate = chosen.refreshRate;
			values.shadows = chosen.shadows;
			values.outdoorDistance = MirrorSceneRange::ClampDistance(static_cast<int>(chosen.objectDistance));
			values.shadowDistance = MirrorSceneRange::ClampDistance(static_cast<int>(chosen.shadowDistance));

			values.shadowResolution = values.shadowDetailResolution = 2048;
			
			values.limitedOutdoorRange = true;
			
		}
		return values;
	}
}
