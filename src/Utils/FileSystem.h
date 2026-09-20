#pragma once

#include "Format.h"
#include "WinApi.h"
#include <algorithm>
#include <filesystem>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <string>
#include <tuple>
#include <vector>

struct SettingsDiffEntry
{
	std::string path;
	std::string aValue;
	std::string bValue;
};

namespace Util
{

	namespace PathHelpers
	{

		std::filesystem::path GetCurrentModuleRealPath();

	}

	namespace FileHelpers
	{

		struct DeletionResult
		{
			bool success;
			std::string errorMessage;
			std::string deletedDescription;
		};

	}

	inline std::vector<std::pair<std::string, std::string>> EnumerateDllVersions(const std::filesystem::path& dir)
	{
		std::vector<std::pair<std::string, std::string>> result;
		try {
			for (const auto& entry : std::filesystem::directory_iterator(dir)) {
				if (entry.is_regular_file() && entry.path().extension() == L".dll") {
					const auto& path = entry.path();
					auto version = Util::GetDllVersion(path.c_str());
					auto name = path.filename().string();
					std::string versionStr = version ? Util::GetFormattedVersion(*version) : "Unknown";
					result.emplace_back(name, versionStr);
				}
			}
		} catch (const std::filesystem::filesystem_error& e) {
			
			logger::warn("Failed to enumerate DLL versions in {}: {}", dir.string(), e.what());
		}
		return result;
	}

	namespace FileSystem
	{

		std::vector<SettingsDiffEntry> DiffJson(const nlohmann::json& userJson, const nlohmann::json& testJson, float epsilon = 0.0001f);

		std::vector<SettingsDiffEntry> LoadJsonDiff(const std::filesystem::path& userPath, const std::filesystem::path& testPath, float epsilon = 0.0001f);
	}
}
