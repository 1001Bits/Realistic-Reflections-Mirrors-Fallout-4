#pragma once

#include <windows.h>
#include "MirrorSceneRenderer.h"

namespace FlatDeferredPlayerCapture
{
	bool NativeWorldTransactionActive() noexcept;
}

namespace MirrorDrawHooks
{

	template<class Function, class... Args>
	void Forward(Function& original, ID3D11DeviceContext* context, std::uint32_t count, Args... args)
	{
		MirrorSceneRenderer::RecordTripwireDraw(context, count);
		if (MirrorSceneRenderer::DrawHookPassthrough()) {
			original(context, count, args...);
			return;
		}
		const bool face = MirrorSceneRenderer::FaceSkinTintDrawOverrideActive();
		const bool eye = MirrorSceneRenderer::MirrorEyeGroup8WindingOverrideActive();
		if (!face && !eye) {
			if (!FlatDeferredPlayerCapture::NativeWorldTransactionActive())
				MirrorSceneRenderer::RecordTripwireDrawPost(context, count);
			original(context, count, args...);
			return;
		}
		const bool ownsFace = face && MirrorSceneRenderer::BeginFaceSkinTintDrawOverride(context);
		const bool ownsEye = eye && MirrorSceneRenderer::BeginMirrorEyeGroup8DrawOverride(context);
		bool completed = false;
		__try {
			MirrorSceneRenderer::RecordTripwireDrawPost(context, count);
			original(context, count, args...);
			completed = true;
		} __finally {
			if (ownsEye) (void)MirrorSceneRenderer::EndMirrorEyeGroup8DrawOverride(context, completed);
			if (ownsFace) (void)MirrorSceneRenderer::EndFaceSkinTintDrawOverride(context, completed);
		}
	}
}
