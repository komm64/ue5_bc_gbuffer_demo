#pragma once

#include "SceneViewExtension.h"
#include "BCGBufferPass.h"

// Thin FSceneViewExtension subclass used for diagnostics only.
//
// The actual G-Buffer compression and substitution is performed by the
// function-pointer hook registered with RegisterBCGBufferSubstituteCallback
// (declared in the engine patch at the top of BasePassRendering.cpp).
// That hook calls BCGBuffer_SubstituteTextures() after the Base Pass, which
// runs AddBCGBufferCompressPass() and replaces GBufferA/B/C in FSceneTextures.
// The engine then rebuilds SceneTextureUniformBuffer so the Lighting Pass reads
// the BC textures with no shader changes required.
class FBCGBufferViewExtension final : public FSceneViewExtensionBase
{
public:
    explicit FBCGBufferViewExtension(const FAutoRegister& AutoRegister)
        : FSceneViewExtensionBase(AutoRegister) {}

    void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
    void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
    void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

    virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

    // Not used — compression is handled by the function-pointer hook.
    void PostRenderBasePassDeferred_RenderThread(
        FRDGBuilder& GraphBuilder,
        FSceneView& InView,
        const FRenderTargetBindingSlots& RenderTargets,
        TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) override;
};

// Called from BCGBufferModule.cpp at startup to register the substitution callback.
void BCGBuffer_RegisterSubstituteCallback();
