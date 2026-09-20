#include "MirrorShaderBytecode.h"
#include <iostream>
#include <vector>

int wmain(int argc,wchar_t** argv) try
{
    if(argc!=2) {std::cerr<<"Usage: MirrorShaderPrecompile <MirrorsOfFallout shader directory>\n";return 2;}
    const std::filesystem::path root=argv[1];
    struct Job {const wchar_t* file;const char* entry;const char* profile;};
    const Job jobs[]{
        {L"PlanarMaterialResolveCS.hlsl","main","cs_5_0"},
        {L"PlanarMirrorLightTilesCS.hlsl","main","cs_5_0"},
        {L"PlanarMirrorOverlayCS.hlsl","VSMain","vs_5_0"},
        {L"PlanarMirrorOverlayCS.hlsl","PSMain","ps_5_0"},
        {L"FlatMirrorEyePS.hlsl","main","ps_5_0"},
        {L"PlanarCandidateProofCS.hlsl","main","cs_5_0"},
        {L"PlanarMirrorFrameDeltaCS.hlsl","main","cs_5_0"},
        {L"PlanarMirrorPlayerOverlayResolveCS.hlsl","main","cs_5_0"},
        {L"PlanarNativeLitPlayerOverlayCS.hlsl","main","cs_5_0"},
    };
    for(const auto& job:jobs) {
        const auto source=root/job.file;
        const D3D_SHADER_MACRO macros[]{{MirrorShaderBytecode::StageDefine(job.profile),""},{"WINPC",""},{"DX11",""},{nullptr,nullptr}};
        Microsoft::WRL::ComPtr<ID3DBlob> errors,code;
        const auto key=MirrorShaderBytecode::Key(source,macros,job.entry,job.profile,MirrorShaderBytecode::Flags,&errors);
        if(!key) {if(errors)std::cerr<<static_cast<const char*>(errors->GetBufferPointer());return 1;}
        const auto destination=root/L"Compiled"/MirrorShaderBytecode::Name(key);
        if(!MirrorShaderBytecode::Read(destination)) {
            errors.Reset();MirrorShaderBytecode::Include include(source);
            if(FAILED(D3DCompileFromFile(source.c_str(),macros,&include,job.entry,job.profile,
                MirrorShaderBytecode::Flags,0,&code,&errors))) {
                if(errors)std::cerr<<static_cast<const char*>(errors->GetBufferPointer());return 1;
            }
            if(!MirrorShaderBytecode::Write(destination,code.Get()) || !MirrorShaderBytecode::Read(destination)) return 1;
        }
        std::wcout<<job.file<<L" -> "<<destination.filename().wstring()<<L'\n';
    }
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
