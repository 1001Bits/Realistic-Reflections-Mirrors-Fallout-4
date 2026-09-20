#include "D3D.h"
#include "MirrorShaderBytecode.h"

#include "Utils/Format.h"
#include <DDSTextureLoader.h>
#include <DirectXTex.h>
#include <d3dcompiler.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Util
{
	ID3D11ShaderResourceView* GetSRVFromRTV(const ID3D11RenderTargetView* a_rtv)
	{
		if (a_rtv) {
			if (auto r = globals::game::renderer) {
				auto rexRTV = reinterpret_cast<const REX::W32::ID3D11RenderTargetView*>(a_rtv);
				for (int i = 0; i < GetRenderTargetCount(); i++) {
					auto rt = r->data.renderTargets[i];
					if (rexRTV == rt.rtView) {
						return reinterpret_cast<ID3D11ShaderResourceView*>(rt.srView);
					}
				}
			}
		}
		return nullptr;
	}

	ID3D11RenderTargetView* GetRTVFromSRV(const ID3D11ShaderResourceView* a_srv)
	{
		if (a_srv) {
			if (auto r = globals::game::renderer) {
				auto rexSRV = reinterpret_cast<const REX::W32::ID3D11ShaderResourceView*>(a_srv);
				for (int i = 0; i < GetRenderTargetCount(); i++) {
					auto rt = r->data.renderTargets[i];
					if (rexSRV == rt.srView || rexSRV == rt.copySRView) {
						return reinterpret_cast<ID3D11RenderTargetView*>(rt.rtView);
					}
				}
			}
		}
		return nullptr;
	}

	std::string GetNameFromSRV(const ID3D11ShaderResourceView* a_srv)
	{
		using RENDER_TARGET = RE::RENDER_TARGETS::RENDER_TARGET;

		if (a_srv) {
			if (auto r = globals::game::renderer) {
				auto rexSRV = reinterpret_cast<const REX::W32::ID3D11ShaderResourceView*>(a_srv);
				for (int i = 0; i < GetRenderTargetCount(); i++) {
					auto rt = r->data.renderTargets[i];
					if (rexSRV == rt.srView || rexSRV == rt.copySRView) {
						return std::string(magic_enum::enum_name(static_cast<RENDER_TARGET>(i)));
					}
				}
			}
		}
		return "NONE";
	}

	std::string GetNameFromRTV(const ID3D11RenderTargetView* a_rtv)
	{
		using RENDER_TARGET = RE::RENDER_TARGETS::RENDER_TARGET;
		if (a_rtv) {
			if (auto r = globals::game::renderer) {
				auto rexRTV = reinterpret_cast<const REX::W32::ID3D11RenderTargetView*>(a_rtv);
				for (int i = 0; i < GetRenderTargetCount(); i++) {
					auto rt = r->data.renderTargets[i];
					if (rexRTV == rt.rtView) {
						return std::string(magic_enum::enum_name(static_cast<RENDER_TARGET>(i)));
					}
				}
			}
		}
		return "NONE";
	}

	GUID WKPDID_D3DDebugObjectNameT = { 0x429b8c22, 0x9188, 0x4b0c, 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00 };

	void SetResourceName(ID3D11DeviceChild* Resource, const char* Format, ...)
	{
		if (!Resource)
			return;

		char buffer[1024];
		va_list va;

		va_start(va, Format);
		int len = _vsnprintf_s(buffer, _TRUNCATE, Format, va);
		va_end(va);

		Resource->SetPrivateData(WKPDID_D3DDebugObjectNameT, len, buffer);
	}

	using CustomInclude = MirrorShaderBytecode::Include;

	namespace
	{
		std::filesystem::path ShaderCachePath(std::uint64_t key) noexcept
		{
			std::error_code error;
			std::array<wchar_t, MAX_PATH> executable{};
			if (GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())) == 0)
				return {};
			auto directory = std::filesystem::path(executable.data()).parent_path() /
				L"Data" / L"F4SE" / L"Plugins" / L"RealisticReflectionsMirrors" / L"ShaderCache";
			std::filesystem::create_directories(directory, error);
			if (error)
				return {};
			wchar_t name[32]{};
			std::swprintf(name, std::size(name), L"%016llX.cso", static_cast<unsigned long long>(key));
			return directory / name;
		}
	}

	ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const std::vector<std::pair<const char*, const char*>>& Defines, const char* ProgramType, const char* Program) noexcept try
	{
		auto device = globals::d3d::device;
		if (!device || !FilePath || !ProgramType || !Program)
			return nullptr;

		CustomInclude include{ FilePath };

		std::vector<D3D_SHADER_MACRO> macros;
		std::string str = Util::WStringToString(FilePath);

		for (auto& i : Defines) {
			if (i.first && _stricmp(i.first, "") != 0) {
				macros.push_back({ i.first, i.second });
			} else {
				logger::error("Failed to process shader defines for {}", str);
			}
		}

		if (REL::Module::IsVR())
			macros.push_back({ "VR", "" });
		if (!_stricmp(ProgramType, "ps_5_0"))
			macros.push_back({ "PSHADER", "" });
		else if (!_stricmp(ProgramType, "vs_5_0"))
			macros.push_back({ "VSHADER", "" });
		else if (!_stricmp(ProgramType, "hs_5_0"))
			macros.push_back({ "HULLSHADER", "" });
		else if (!_stricmp(ProgramType, "ds_5_0"))
			macros.push_back({ "DOMAINSHADER", "" });
		else if (!_stricmp(ProgramType, "cs_5_0"))
			macros.push_back({ "COMPUTESHADER", "" });
		else if (!_stricmp(ProgramType, "cs_4_0"))
			macros.push_back({ "COMPUTESHADER", "" });
		else if (!_stricmp(ProgramType, "cs_5_1"))
			macros.push_back({ "COMPUTESHADER", "" });
		else
			return nullptr;

		macros.push_back({ "WINPC", "" });
		macros.push_back({ "DX11", "" });
		macros.push_back({ nullptr, nullptr });

		constexpr uint32_t flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

		Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
		Microsoft::WRL::ComPtr<ID3DBlob> shaderErrors;

		if (!std::filesystem::exists(FilePath)) {
			logger::error("Failed to compile shader; {} does not exist", str);
			return nullptr;
		}
		const auto cacheKey = MirrorShaderBytecode::Key(FilePath, macros.data(), Program, ProgramType, flags,
            shaderErrors.GetAddressOf());
        if (!cacheKey) {
            logger::warn("Shader preprocessing failed: {}: {}", str,
                shaderErrors ? static_cast<const char*>(shaderErrors->GetBufferPointer()) : "source unavailable");
            return nullptr;
        }
        shaderErrors.Reset();
        const auto packagedFile = std::filesystem::path(FilePath).parent_path() / L"Compiled" /
            MirrorShaderBytecode::Name(cacheKey);
        shaderBlob = MirrorShaderBytecode::Read(packagedFile);
        const bool packaged = shaderBlob != nullptr;
        const auto cacheFile = ShaderCachePath(cacheKey);
        if (!shaderBlob && !cacheFile.empty()) shaderBlob = MirrorShaderBytecode::Read(cacheFile);
        const bool served = shaderBlob != nullptr;
        logger::info("[MirrorShaders] {} entry={} profile={} source={} key={:016X}", str, Program, ProgramType,
            packaged ? "package" : (served ? "cache" : "compile"), cacheKey);
		if (!served) {
			logger::debug("Compiling {} with {}", str, DefinesToString(macros));
			if (FAILED(D3DCompileFromFile(FilePath, macros.data(), &include, Program, ProgramType, flags, 0, shaderBlob.GetAddressOf(), shaderErrors.GetAddressOf()))) {
				logger::warn("Shader compilation failed:\n\n{}", shaderErrors ? static_cast<char*>(shaderErrors->GetBufferPointer()) : "Unknown error");
				return nullptr;
			}
            
            if (!cacheFile.empty()) (void)MirrorShaderBytecode::Write(cacheFile,shaderBlob.Get());

		}
		if (shaderErrors)
			logger::debug("Shader logs:\n{}", static_cast<char*>(shaderErrors->GetBufferPointer()));
		if (!_stricmp(ProgramType, "ps_5_0")) {
			Microsoft::WRL::ComPtr<ID3D11PixelShader> regShader;
			if (FAILED(device->CreatePixelShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, regShader.GetAddressOf()))) {
				logger::warn("Shader creation failed: {} ({})", str, ProgramType);
				return nullptr;
			}
			return regShader.Detach();
		} else if (!_stricmp(ProgramType, "vs_5_0")) {
			Microsoft::WRL::ComPtr<ID3D11VertexShader> regShader;
			if (FAILED(device->CreateVertexShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, regShader.GetAddressOf()))) {
				logger::warn("Shader creation failed: {} ({})", str, ProgramType);
				return nullptr;
			}
			return regShader.Detach();
		} else if (!_stricmp(ProgramType, "hs_5_0")) {
			Microsoft::WRL::ComPtr<ID3D11HullShader> regShader;
			if (FAILED(device->CreateHullShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, regShader.GetAddressOf()))) {
				logger::warn("Shader creation failed: {} ({})", str, ProgramType);
				return nullptr;
			}
			return regShader.Detach();
		} else if (!_stricmp(ProgramType, "ds_5_0")) {
			Microsoft::WRL::ComPtr<ID3D11DomainShader> regShader;
			if (FAILED(device->CreateDomainShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, regShader.GetAddressOf()))) {
				logger::warn("Shader creation failed: {} ({})", str, ProgramType);
				return nullptr;
			}
			return regShader.Detach();
		} else if (!_stricmp(ProgramType, "cs_5_0")) {
			Microsoft::WRL::ComPtr<ID3D11ComputeShader> regShader;
			if (FAILED(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, regShader.GetAddressOf()))) {
				logger::warn("Shader creation failed: {} ({})", str, ProgramType);
				return nullptr;
			}
			return regShader.Detach();
		} else if (!_stricmp(ProgramType, "cs_4_0")) {
			Microsoft::WRL::ComPtr<ID3D11ComputeShader> regShader;
			if (FAILED(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, regShader.GetAddressOf()))) {
				logger::warn("Shader creation failed: {} ({})", str, ProgramType);
				return nullptr;
			}
			return regShader.Detach();
		}

		return nullptr;
	}
	catch (...) {
		
		return nullptr;
	}

	class ScopedD3DMap
	{
	public:
		ScopedD3DMap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP mapType, UINT mapFlags) :
			context_(context), resource_(resource), subresource_(subresource), mapped_(false)
		{
			if (context && resource) {
				HRESULT hr = context->Map(resource, subresource, mapType, mapFlags, &mappedResource_);
				mapped_ = SUCCEEDED(hr);
			}
		}

		~ScopedD3DMap()
		{
			if (mapped_ && context_ && resource_) {
				context_->Unmap(resource_, subresource_);
			}
		}

		ScopedD3DMap(const ScopedD3DMap&) = delete;
		ScopedD3DMap& operator=(const ScopedD3DMap&) = delete;

		bool IsValid() const { return mapped_; }
		D3D11_MAPPED_SUBRESOURCE* GetMappedResource() { return mapped_ ? &mappedResource_ : nullptr; }

	private:
		ID3D11DeviceContext* context_;
		ID3D11Resource* resource_;
		UINT subresource_;
		bool mapped_;
		D3D11_MAPPED_SUBRESOURCE mappedResource_{};
	};

	bool ValidateHighlightColor(const std::array<float, 4>& color)
	{
		for (size_t i = 0; i < 4; ++i) {
			if (color[i] < 0.0f || color[i] > 1.0f || !std::isfinite(color[i])) {
				return false;
			}
		}
		return true;
	}

	void ApplyHighlightTintToTexture(ID3D11Texture2D* texture, bool isHighlighted, const std::array<float, 4>& highlightColor)
	{
		if (!isHighlighted || !texture)
			return;

		if (!ValidateHighlightColor(highlightColor)) {
			logger::error("ApplyHighlightTintToTexture: Invalid highlight color values. All components must be finite and in range [0.0, 1.0]");
			return;
		}

		D3D11_TEXTURE2D_DESC desc;
		texture->GetDesc(&desc);

		const UINT pixelCount = desc.Width * desc.Height;
		const UINT largeTextureThreshold = 1024 * 1024;  
		if (pixelCount > largeTextureThreshold) {
			static std::once_flag largeTextureWarning;
			std::call_once(largeTextureWarning, [&]() {
				logger::warn("ApplyHighlightTintToTexture: Processing large texture ({}x{} = {} pixels). Consider using compute shader for better performance. This warning will only be shown once.",
					desc.Width, desc.Height, pixelCount);
			});
		}  
		ID3D11Texture2D* stagingTexture = nullptr;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = globals::d3d::device->CreateTexture2D(&desc, nullptr, &stagingTexture);
		if (FAILED(hr)) {
			logger::error("ApplyHighlightTintToTexture: Failed to create staging texture (HRESULT: 0x{:08X})", static_cast<uint32_t>(hr));
			return;
		}

		std::unique_ptr<ID3D11Texture2D, void (*)(ID3D11Texture2D*)> stagingGuard(
			stagingTexture, [](ID3D11Texture2D* tex) { if (tex) tex->Release(); });

		globals::d3d::context->CopyResource(stagingTexture, texture);

		ScopedD3DMap scopedMap(globals::d3d::context, stagingTexture, 0, D3D11_MAP_READ_WRITE, 0);
		if (!scopedMap.IsValid()) {
			logger::error("ApplyHighlightTintToTexture: Failed to map staging texture");
			return;
		}

		D3D11_MAPPED_SUBRESOURCE* mapped = scopedMap.GetMappedResource();
		if (!mapped || !mapped->pData) {
			logger::error("ApplyHighlightTintToTexture: Invalid mapped resource data");
			return;
		}

		uint8_t* pixels = static_cast<uint8_t*>(mapped->pData);
		const float blendAlpha = highlightColor[3];

		for (UINT y = 0; y < desc.Height; ++y) {
			for (UINT x = 0; x < desc.Width; ++x) {
				uint8_t* pixel = pixels + (y * mapped->RowPitch + x * 4);

				if (pixel[3] > 0) {

					float originalR = pixel[0] / 255.0f;
					float originalG = pixel[1] / 255.0f;
					float originalB = pixel[2] / 255.0f;

					pixel[0] = static_cast<uint8_t>((originalR * (1.0f - blendAlpha) + highlightColor[0] * blendAlpha) * 255.0f);
					pixel[1] = static_cast<uint8_t>((originalG * (1.0f - blendAlpha) + highlightColor[1] * blendAlpha) * 255.0f);
					pixel[2] = static_cast<uint8_t>((originalB * (1.0f - blendAlpha) + highlightColor[2] * blendAlpha) * 255.0f);
					
				}
			}
		}

		globals::d3d::context->CopyResource(texture, stagingTexture);

	}

	HRESULT CreateOverlayTextureAndRTV(ID3D11Device* device, int width, int height, ID3D11Texture2D** outTex, ID3D11RenderTargetView** outRTV)
	{
		
		if (!device) {
			logger::error("CreateOverlayTextureAndRTV: device parameter is null");
			return E_INVALIDARG;
		}

		if (!outTex || !outRTV) {
			logger::error("CreateOverlayTextureAndRTV: output parameters cannot be null");
			return E_INVALIDARG;
		}

		if (width <= 0 || height <= 0) {
			logger::error("CreateOverlayTextureAndRTV: invalid dimensions ({}x{})", width, height);
			return E_INVALIDARG;
		}

		*outTex = nullptr;
		*outRTV = nullptr;

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		HRESULT hr = device->CreateTexture2D(&desc, nullptr, outTex);
		if (FAILED(hr)) {
			logger::error("CreateOverlayTextureAndRTV: Failed to create texture (HRESULT: 0x{:08X})", static_cast<uint32_t>(hr));
			return hr;
		}

		hr = device->CreateRenderTargetView(*outTex, nullptr, outRTV);
		if (FAILED(hr)) {
			logger::error("CreateOverlayTextureAndRTV: Failed to create render target view (HRESULT: 0x{:08X})", static_cast<uint32_t>(hr));
			
			if (*outTex) {
				(*outTex)->Release();
				*outTex = nullptr;
			}
			return hr;
		}

		return S_OK;
	}

	HRESULT SaveTextureToFile(ID3D11Device* device, ID3D11DeviceContext* context, const std::filesystem::path& path, ID3D11Texture2D* tex)
	{
		
		if (!device) {
			logger::error("SaveTextureToFile: device parameter is null");
			return E_INVALIDARG;
		}

		if (!context) {
			logger::error("SaveTextureToFile: context parameter is null");
			return E_INVALIDARG;
		}

		if (!tex) {
			logger::error("SaveTextureToFile: texture paramater is null");
			return E_INVALIDARG;
		}

		if (path.empty()) {
			logger::error("SaveTextureToFile: path parameter is empty");
			return E_INVALIDARG;
		}

		namespace fs = std::filesystem;

		DirectX::ScratchImage cpuImage;
		if (const auto hr = CaptureTexture(device, context, tex, cpuImage); FAILED(hr))
			return hr;

		const auto parent = path.parent_path();
		std::error_code ec;
		if (!parent.empty() && !fs::exists(parent, ec)) {
			if (!fs::create_directories(parent, ec)) {
				logger::error("SaveTextureToFile: failed to create directories");
				return HRESULT_FROM_WIN32(static_cast<DWORD>(ec.value()));
			}
		}

		const std::wstring tmp = path.wstring().append(L".tmp");
		if (const auto hr = SaveToDDSFile(cpuImage.GetImages(), cpuImage.GetImageCount(), cpuImage.GetMetadata(), DirectX::DDS_FLAGS_NONE, tmp.c_str()); FAILED(hr)) {
			logger::error("SaveTextureToFile: failed to save to file");
			fs::remove(tmp);
			return hr;
		}

		fs::rename(tmp, path, ec);
		if (ec) {
			fs::remove(path, ec);
			if (ec) {
				logger::error("SaveTextureToFile: failed to remove existing file");
				fs::remove(tmp);
				return HRESULT_FROM_WIN32(static_cast<DWORD>(ec.value()));
			}
			fs::rename(tmp, path, ec);
			if (ec) {
				logger::error("SaveTextureToFile: failed to rename file");
				fs::remove(tmp);
				return HRESULT_FROM_WIN32(static_cast<DWORD>(ec.value()));
			}
		}

		return S_OK;
	}

	HRESULT LoadTextureFromFile(ID3D11Device* device, const std::filesystem::path& path, ID3D11Texture2D** outTex, ID3D11ShaderResourceView** outSRV)
	{
		
		if (!device) {
			logger::error("LoadTextureFromFile: device parameter is null");
			return E_INVALIDARG;
		}

		if (path.empty()) {
			logger::error("LoadTextureFromFile: path parameter is empty");
			return E_INVALIDARG;
		}

		*outTex = nullptr;
		*outSRV = nullptr;

		ID3D11Resource* resource;
		if (const auto hr = DirectX::CreateDDSTextureFromFile(device, path.wstring().c_str(), &resource, outSRV); FAILED(hr)) {
			logger::error("LoadTextureFromFile: failed to load resource from file");
			return hr;
		}

		const auto hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(outTex));
		resource->Release();
		resource = nullptr;

		if (FAILED(hr)) {
			logger::error("LoadTextureFromFile: failed to query texture interface");
			(*outSRV)->Release();
			*outSRV = nullptr;
			*outTex = nullptr;
			return hr;
		}

		return S_OK;
	}
}  
