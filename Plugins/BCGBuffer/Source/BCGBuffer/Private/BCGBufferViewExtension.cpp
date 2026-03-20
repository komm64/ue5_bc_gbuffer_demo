#include "BCGBufferViewExtension.h"
#include "BCGBufferPass.h"

#include "SceneRendering.h"      // FViewInfo
#include "SceneTexturesConfig.h" // FSceneTextureUniformParameters (ENGINE_API)
#include "SceneTextures.h"       // FSceneTextures (Renderer/Internal)

DEFINE_LOG_CATEGORY_STATIC(LogBCGBuffer, Log, All);

// ---------------------------------------------------------------------------
// Engine function-pointer hook declared in BasePassRendering.cpp
// ---------------------------------------------------------------------------

extern void RegisterBCGBufferSubstituteCallback(
    void (*Callback)(FRDGBuilder&, const FSceneView&, FSceneTextures&));

// ---------------------------------------------------------------------------
// Callback — compresses GBufferC then substitutes it in FSceneTextures.
// Called from our hook in BasePassRendering.cpp after the Base Pass.
// SceneTextures.GBufferC here is the REAL GBuffer texture (correct extent).
// ---------------------------------------------------------------------------

static void BCGBuffer_SubstituteTextures(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FSceneTextures& SceneTextures)
{
    if (!IsBCGBufferEnabled())
        return;

    if (!SceneTextures.GBufferC)
        return;

    const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);
    const FIntPoint Extent = SceneTextures.GBufferC->Desc.Extent;

    static bool bLoggedOnce = false;
    if (!bLoggedOnce)
    {
        bLoggedOnce = true;
        UE_LOG(LogBCGBuffer, Warning,
            TEXT("BCGBuffer_SubstituteTextures: GBufferC=%dx%d Samples=%d Flags=0x%llx GBufferA=%s GBufferB=%s"),
            Extent.X, Extent.Y,
            SceneTextures.GBufferC->Desc.NumSamples,
            (uint64)SceneTextures.GBufferC->Desc.Flags,
            SceneTextures.GBufferA ? TEXT("ok") : TEXT("null"),
            SceneTextures.GBufferB ? TEXT("ok") : TEXT("null"));
    }

    FBCGBufferTextures Compressed = AddBCGBufferCompressPass(
        GraphBuilder, ViewInfo, Extent,
        SceneTextures.GBufferC,  // BaseColor + AO
        SceneTextures.GBufferA,  // Normal + PerObjectData
        SceneTextures.GBufferB); // ORM + ShadingModelID

    if (Compressed.CompressedC) SceneTextures.GBufferC = Compressed.CompressedC;
    if (Compressed.CompressedA) SceneTextures.GBufferA = Compressed.CompressedA;
    if (Compressed.CompressedB) SceneTextures.GBufferB = Compressed.CompressedB;
}

// ---------------------------------------------------------------------------
// Registration helper — called from BCGBufferModule.cpp at startup
// ---------------------------------------------------------------------------

void BCGBuffer_RegisterSubstituteCallback()
{
    RegisterBCGBufferSubstituteCallback(&BCGBuffer_SubstituteTextures);
    UE_LOG(LogBCGBuffer, Warning, TEXT("BCGBuffer_RegisterSubstituteCallback: callback registered"));
}

// ---------------------------------------------------------------------------
// Diagnostic: IsActiveThisFrame_Internal
// ---------------------------------------------------------------------------

bool FBCGBufferViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
    static bool bLoggedOnce = false;
    if (!bLoggedOnce)
    {
        UE_LOG(LogBCGBuffer, Warning, TEXT("IsActiveThisFrame_Internal called"));
        bLoggedOnce = true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// SetupViewFamily
// ---------------------------------------------------------------------------

void FBCGBufferViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
}

// ---------------------------------------------------------------------------
// PostRenderBasePassDeferred_RenderThread — no longer used for compression.
// Compression now happens inside BCGBuffer_SubstituteTextures (the hook in
// BasePassRendering.cpp) where SceneTextures.GBufferC has the real extent.
// ---------------------------------------------------------------------------

void FBCGBufferViewExtension::PostRenderBasePassDeferred_RenderThread(
    FRDGBuilder& GraphBuilder,
    FSceneView& InView,
    const FRenderTargetBindingSlots& RenderTargets,
    TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
    // intentionally empty — compression is handled via GBCGBuffer_SubstituteCallback
}
