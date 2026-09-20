

#pragma once
namespace Util
{
	std::string GetFormattedVersion(const REL::Version& version);

	std::string DefinesToString(const std::vector<std::pair<const char*, const char*>>& defines);
	std::string DefinesToString(const std::vector<D3D_SHADER_MACRO>& defines);

	std::string FixFilePath(const std::string& a_path);
	std::string WStringToString(const std::wstring& wideString);

	std::string FormatMilliseconds(float ms);

	std::string FormatMicroseconds(float us);

	std::string FormatPercent(float percent);

	std::string FormatFileSize(uint64_t bytes);

	std::string TimeAgoString(std::chrono::steady_clock::time_point last);

	std::string TimeAgoStringQPC(const LARGE_INTEGER& lastTime, const LARGE_INTEGER& frequency);

	std::string FormatTimeAgo(std::filesystem::file_time_type fileTime);

	std::string FormatDeltaWithPercent(float a, float b, float threshold = 0.01f);

	float CalculatePercentage(float part, float total, float defaultValue = 0.0f);

	float CalculateCostPerCall(float frameTime, float drawCalls);

	float CalculateOtherFrameTime(float totalFrameTime, float measuredSum);
}  