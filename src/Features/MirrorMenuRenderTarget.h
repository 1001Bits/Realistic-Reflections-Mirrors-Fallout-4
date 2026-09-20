#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>
#include "../Utils/D3DDeviceIdentity.h"

namespace MirrorMenuRenderTarget
{
    class Scope
    {
    public:
        Scope() = default;
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        ~Scope() { Restore(); }

        bool Begin(ID3D11DeviceContext* context, ID3D11RenderTargetView* screenTarget) noexcept
        {
            if (depth_) { ++depth_; return true; }
            if (!context || !screenTarget || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
            if (source_.Get() != context) {
                menuState_.Reset(); context_.Reset(); device_.Reset();
                source_ = context;
                context->GetDevice(device_.GetAddressOf());
                Microsoft::WRL::ComPtr<ID3D11Device1> device1;
                if (!device_ || FAILED(device_.As(&device1)) || FAILED(source_.As(&context_))) return false;
                const auto level = device_->GetFeatureLevel();
                const UINT flags = (device_->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) ?
                    D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
                if (FAILED(device1->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION,
                        __uuidof(ID3D11Device1), nullptr, menuState_.GetAddressOf()))) return false;
            }
            if (!context_ || !menuState_) return false;
            Microsoft::WRL::ComPtr<ID3D11Device> targetDevice;
            screenTarget->GetDevice(targetDevice.GetAddressOf());

            if (!Util::D3DDevicesMatch(targetDevice.Get(), device_.Get())) return false;

            context_->SwapDeviceContextState(menuState_.Get(), previous_.GetAddressOf());
            context_->OMSetRenderTargets(1, &screenTarget, nullptr);
            depth_ = 1;
            return true;
        }

        void End() noexcept
        {
            if (depth_ && --depth_ == 0) Restore();
        }

        bool Active() const noexcept { return depth_ != 0; }

    private:
        void Restore() noexcept
        {
            if (previous_) {

                context_->ClearState();
                context_->SwapDeviceContextState(previous_.Get(), nullptr);
                previous_.Reset();
            }
            depth_ = 0;
        }

        Microsoft::WRL::ComPtr<ID3D11DeviceContext> source_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context_;
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3DDeviceContextState> menuState_, previous_;
        unsigned depth_{};
    };
}
