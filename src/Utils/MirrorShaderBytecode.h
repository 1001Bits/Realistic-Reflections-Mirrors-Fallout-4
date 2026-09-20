#pragma once
#include <windows.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace MirrorShaderBytecode
{
	struct Include : public ID3DInclude
	{
		explicit Include(const std::filesystem::path& source) : sourceDirectory(source.parent_path()) {}

		HRESULT Open(D3D_INCLUDE_TYPE includeType, LPCSTR fileName, LPCVOID parentData,
			LPCVOID* data, UINT* bytes) noexcept override
		{
			if (!data || !bytes) return E_INVALIDARG;
			*data = nullptr;
			*bytes = 0;
			if (!fileName) return E_INVALIDARG;
			try {

				const auto parent = files.find(parentData);
				const auto& directory = parent != files.end() ? parent->second.directory : sourceDirectory;
				auto path = includeType == D3D_INCLUDE_LOCAL ? directory / fileName :
					std::filesystem::path(L"Data\\Shaders") / fileName;
				std::ifstream file(path, std::ios::binary | std::ios::ate);
				if (!file.is_open() && includeType == D3D_INCLUDE_LOCAL) {
					path = std::filesystem::path(L"Data\\Shaders") / fileName;
					file.clear();
					file.open(path, std::ios::binary | std::ios::ate);
				}
				if (!file.is_open()) return E_FAIL;
				const auto length = file.tellg();
				if (length < 0 || static_cast<std::uint64_t>(length) >
					(std::numeric_limits<UINT>::max)()) return E_FAIL;
				const auto size = static_cast<UINT>(length);
				auto buffer = std::make_unique<char[]>((std::max)(1u, size));
				file.seekg(0, std::ios::beg);
				if (!file.read(buffer.get(), size)) return E_FAIL;

				if (!size) buffer[0] = '\n';
				const auto* pointer = buffer.get();
				files.emplace(pointer, File{path.parent_path(), std::move(buffer)});
				*data = pointer;
				*bytes = (std::max)(1u, size);
				return S_OK;
			} catch (...) {
				return E_FAIL;
			}
		}

		HRESULT Close(LPCVOID data) noexcept override
		{
			return files.erase(data) ? S_OK : E_INVALIDARG;
		}
	private:
		struct File { std::filesystem::path directory; std::unique_ptr<char[]> buffer; };
		std::filesystem::path sourceDirectory;
		std::unordered_map<LPCVOID, File> files;
	};

    constexpr UINT Flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

    inline const char* StageDefine(const char* profile) noexcept
    {
        if (!_stricmp(profile,"ps_5_0")) return "PSHADER";
        if (!_stricmp(profile,"vs_5_0")) return "VSHADER";
        if (!_stricmp(profile,"hs_5_0")) return "HULLSHADER";
        if (!_stricmp(profile,"ds_5_0")) return "DOMAINSHADER";
        if (!_stricmp(profile,"cs_5_0") || !_stricmp(profile,"cs_4_0") || !_stricmp(profile,"cs_5_1")) return "COMPUTESHADER";
        return nullptr;
    }

    inline std::uint64_t Key(const std::filesystem::path& path, const D3D_SHADER_MACRO* macros,
        const char* entry, const char* profile, UINT flags, ID3DBlob** errors)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> source, expanded;
        if (FAILED(D3DReadFileToBlob(path.c_str(),&source))) return 0;
        Include include(path);
        
        const auto name=path.filename().string();
        if (FAILED(D3DPreprocess(source->GetBufferPointer(),source->GetBufferSize(),name.c_str(),
                macros,&include,&expanded,errors))) return 0;
        std::uint64_t hash=14695981039346656037ull;
        const auto mix=[&](const void* input,std::size_t size) {
            const auto* bytes=static_cast<const unsigned char*>(input);
            for(std::size_t i=0;i<size;++i) {hash^=bytes[i];hash*=1099511628211ull;}
        };
        constexpr char version[]="Mirrors bytecode v3 / d3dcompiler47";
        mix(version,sizeof(version));mix(entry,std::strlen(entry)+1);mix(profile,std::strlen(profile)+1);
        mix(&flags,sizeof(flags));

        std::string_view sourceText(static_cast<const char*>(expanded->GetBufferPointer()),
            expanded->GetBufferSize());
        while (!sourceText.empty()) {
            const auto end=sourceText.find('\n');
            const auto length=end==std::string_view::npos ? sourceText.size() : end+1;
            const auto line=sourceText.substr(0,length);
            const auto first=line.find_first_not_of(" \t\r");
            const auto directive=first==std::string_view::npos ? std::string_view{} : line.substr(first);
            if (!directive.starts_with("#line ") && !directive.starts_with("#line\t"))
                mix(line.data(),line.size());
            sourceText.remove_prefix(length);
        }
        return hash ? hash : 1;
    }
    inline std::filesystem::path Name(std::uint64_t key)
    {
        wchar_t name[32]{};
        std::swprintf(name,std::size(name),L"%016llX.cso",static_cast<unsigned long long>(key));
        return name;
    }
    inline Microsoft::WRL::ComPtr<ID3DBlob> Read(const std::filesystem::path& path)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> code;
        std::error_code error;
        const auto size=std::filesystem::file_size(path,error);
        if(error || size<32 || size>(64u<<20) || FAILED(D3DReadFileToBlob(path.c_str(),&code))) return {};
        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
        if(FAILED(D3DReflect(code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&reflection)))) return {};
        return code;
    }
    inline bool Write(const std::filesystem::path& path, ID3DBlob* code)
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(),error);
        return !error && code && SUCCEEDED(D3DWriteBlobToFile(code,path.c_str(),TRUE));
    }
}
