#include "FileSystem.h"
#include <Windows.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <psapi.h>

namespace Util
{
	
	namespace PathHelpers
	{
		std::filesystem::path GetCurrentModuleRealPath()
		{
			try {
				HMODULE selfModule = nullptr;
				if (!GetModuleHandleExW(
						GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						reinterpret_cast<LPCWSTR>(&GetCurrentModuleRealPath),
						&selfModule)) {
					return {};
				}
				wchar_t buffer[MAX_PATH]{};
				DWORD size = GetModuleFileNameExW(GetCurrentProcess(), selfModule, buffer, MAX_PATH);
				if (size == 0 || size == MAX_PATH) {
					throw std::runtime_error("Failed to get module filename");
				}
				return std::filesystem::path(buffer);
			} catch (const std::exception& e) {
				logger::error("GetCurrentModuleRealPath: Exception caught: {}", e.what());
				return {};
			}
		}

	}

	namespace FileHelpers
	{
	}
}

std::vector<SettingsDiffEntry> Util::FileSystem::DiffJson(const nlohmann::json& userJson, const nlohmann::json& testJson, float epsilon)
{
	std::vector<SettingsDiffEntry> diffEntries;

	try {
		auto diff = nlohmann::json::diff(userJson, testJson);

		for (const auto& change : diff) {
			try {
				std::string op = change.value("op", "");
				std::string path = change.value("path", "");
				std::string aVal, bVal;

				if (op == "replace") {
					auto aJson = userJson.at(nlohmann::json::json_pointer(path));
					auto bJson = testJson.at(nlohmann::json::json_pointer(path));

					if (aJson.is_number() && bJson.is_number()) {
						double aDouble = aJson.get<double>();
						double bDouble = bJson.get<double>();
						if (std::abs(aDouble - bDouble) < static_cast<double>(epsilon)) {
							continue;  
						}
					}

					aVal = aJson.dump();
					bVal = bJson.dump();
				} else if (op == "add") {
					aVal = "(none)";
					bVal = testJson.at(nlohmann::json::json_pointer(path)).dump();
				} else if (op == "remove") {
					aVal = userJson.at(nlohmann::json::json_pointer(path)).dump();
					bVal = "(none)";
				} else {
					logger::warn("Unknown JSON diff operation '{}' at path '{}'", op, path);
					continue;
				}

				diffEntries.push_back({ path, aVal, bVal });
			} catch (const std::exception& e) {
				logger::warn("Failed to process JSON diff change: {}", e.what());
				
			}
		}
	} catch (const std::exception& e) {
		logger::warn("Failed to compute JSON diff: {}", e.what());
	}

	return diffEntries;
}

std::vector<SettingsDiffEntry> Util::FileSystem::LoadJsonDiff(const std::filesystem::path& userPath, const std::filesystem::path& testPath, float epsilon)
{
	std::vector<SettingsDiffEntry> diffEntries;

	try {
		if (!std::filesystem::exists(userPath)) {
			logger::warn("User config file does not exist: {}", userPath.string());
			return diffEntries;
		}

		if (!std::filesystem::exists(testPath)) {
			logger::warn("Test config file does not exist: {}", testPath.string());
			return diffEntries;
		}

		std::ifstream userFile(userPath);
		std::ifstream testFile(testPath);

		if (!userFile.is_open()) {
			logger::warn("Failed to open user config file: {}", userPath.string());
			return diffEntries;
		}

		if (!testFile.is_open()) {
			logger::warn("Failed to open test config file: {}", testPath.string());
			return diffEntries;
		}

		nlohmann::json userJson, testJson;

		try {
			userFile >> userJson;
		} catch (const std::exception& e) {
			logger::warn("Failed to parse user config JSON from '{}': {}", userPath.string(), e.what());
			return diffEntries;
		}

		try {
			testFile >> testJson;
		} catch (const std::exception& e) {
			logger::warn("Failed to parse test config JSON from '{}': {}", testPath.string(), e.what());
			return diffEntries;
		}

		return DiffJson(userJson, testJson, epsilon);
	} catch (const std::exception& e) {
		logger::warn("Failed to load JSON diff from '{}' and '{}': {}", userPath.string(), testPath.string(), e.what());
	}

	return diffEntries;
}
