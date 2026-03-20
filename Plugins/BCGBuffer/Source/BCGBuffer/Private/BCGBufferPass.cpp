#include "BCGBufferPass.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "ShaderParameterStruct.h"
#include "RHIGlobals.h"  // GRHISupportsUAVFormatAliasing

DECLARE_GPU_STAT(BCGBufferCompress);

// ---------------------------------------------------------------------------
// Console variables
// ---------------------------------------------------------------------------

static TAutoConsoleVariable<int32> CVarBCGBufferEnable(
    TEXT("r.BCGBuffer.Enable"),
    0,
    TEXT("0=disabled  1=compress G-Buffer to BC immediately after the Base Pass"),
    ECVF_RenderThreadSafe);

bool IsBCGBufferEnabled()
{
    return CVarBCGBufferEnable.GetValueOnRenderThread() != 0;
}

// ---------------------------------------------------------------------------
// Compute shader
// ---------------------------------------------------------------------------

class FBCGBufferEncodeCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FBCGBufferEncodeCS);
    SHADER_USE_PARAMETER_STRUCT(FBCGBufferEncodeCS, FGlobalShader);

    // 0 = BC1  BaseColor RGB    -> RWTexture2D<uint2>  (8 bytes/block, direct write)
    // 1 = BC5  Normal   RG      -> RWTexture2D<uint4>  (16 bytes/block, direct write)
    // 2 = BC4  Single   R       -> RWTexture2D<uint2>  (8 bytes/block, direct write)
    // 3 = BC3  round-trip RGBA  -> RWTexture2D<float4> full-res decoded  [GBufferC BaseColor+AO]
    // 4 = BC5  round-trip RG+BA -> RWTexture2D<float4> full-res decoded  [GBufferA Normal, BA passthrough]
    // 5 = BC1  round-trip RGB+A -> RWTexture2D<float4> full-res decoded  [GBufferB ORM, A passthrough]
    // 6 = BC3  RGBA direct      -> RWTexture2D<uint4>  (16 bytes/block) [BC4 alpha + BC1 RGB]
    class FEncodeModeDim : SHADER_PERMUTATION_INT("ENCODE_MODE", 7);
    using FPermutationDomain = TShaderPermutationDomain<FEncodeModeDim>;

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SrcTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>,   OutTextureBlock2)   // BC1 / BC4 direct
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint4>,   OutTextureBlock4)   // BC3 / BC5 direct
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>,  OutTextureRGBA)     // round-trip (unused by buffer-copy path)
        SHADER_PARAMETER(FIntPoint, SrcSize)
        SHADER_PARAMETER(FIntPoint, BlockCount)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Params)
    {
        return IsFeatureLevelSupported(Params.Platform, ERHIFeatureLevel::SM5);
    }
};

IMPLEMENT_GLOBAL_SHADER(
    FBCGBufferEncodeCS,
    "/Plugin/BCGBuffer/Private/BCGBufferEncode.usf",
    "BCGBufferEncodeCS",
    SF_Compute);

// ---------------------------------------------------------------------------
// Per-channel compression descriptor
// ---------------------------------------------------------------------------

struct FBCChannelDesc
{
    EPixelFormat BCFormat;      // Output BC format (used on both paths)
    EPixelFormat UAVFormat;     // Raw-uint UAV reinterpret format (UAV aliasing path only)
    bool         bBlock2;       // true = 8-byte block (uint2); false = 16-byte (uint4)
    int32        EncodeMode;    // ENCODE_MODE for direct BC write (modes 0/2/6)
    const TCHAR* Name;
};

// Index 0: GBufferC (BaseColor.rgb + AO.a)
//   BC1 (8 bytes/block, 8:1 vs RGBA8) — AO channel sacrificed
//
// Index 1: GBufferA (Normal oct-encoded RGBA)
//   BC3 (16 bytes/block, 4:1 vs RGBA8) — all 4 channels preserved
//
// Index 2: GBufferB (ORM.rgb + ShadingModelID.a)
//   BC3 (16 bytes/block, 4:1 vs RGBA8) — ShadingModelID (alpha) preserved
static const FBCChannelDesc GBCChannelDescs[] =
{
    { PF_DXT1, PF_R32G32_UINT,       true,  0, TEXT("GBuffer.BaseColorBC1") },
    { PF_DXT5, PF_R32G32B32A32_UINT, false, 6, TEXT("GBuffer.NormalBC3")   },
    { PF_DXT5, PF_R32G32B32A32_UINT, false, 6, TEXT("GBuffer.ORMBC3")      },
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    static constexpr int32 kGroupSize = 8;

    FRDGTextureRef DispatchEncode(
        FRDGBuilder&            GraphBuilder,
        const FGlobalShaderMap* ShaderMap,
        FRDGTextureRef          SrcTexture,
        FIntPoint               SrcExtent,
        const FBCChannelDesc&   Desc)
    {
        const FIntPoint BlockCount = FIntPoint(
            FMath::DivideAndRoundUp(SrcExtent.X, 4),
            FMath::DivideAndRoundUp(SrcExtent.Y, 4));

        if (GRHISupportsUAVFormatAliasing)
        {
            // === Direct UAV aliasing path (AMD / NVIDIA) ===
            // Write BC block data directly to a BC-format texture via an aliased
            // raw-uint UAV.  One compute pass, lowest overhead.
            FRDGTextureDesc TexDesc = FRDGTextureDesc::Create2D(
                SrcExtent, Desc.BCFormat, FClearValueBinding::None,
                TexCreate_ShaderResource | TexCreate_NoFastClear | TexCreate_UAV);
            TexDesc.UAVFormat = Desc.UAVFormat;
            FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(TexDesc, Desc.Name);

            FBCGBufferEncodeCS::FPermutationDomain Perm;
            Perm.Set<FBCGBufferEncodeCS::FEncodeModeDim>(Desc.EncodeMode);
            auto Shader = TShaderMapRef<FBCGBufferEncodeCS>(ShaderMap, Perm);

            auto* Params = GraphBuilder.AllocParameters<FBCGBufferEncodeCS::FParameters>();
            Params->SrcTexture       = SrcTexture;
            Params->SrcSize          = SrcExtent;
            Params->BlockCount       = BlockCount;
            Params->OutTextureBlock2 = nullptr;
            Params->OutTextureBlock4 = nullptr;
            Params->OutTextureRGBA   = nullptr;

            if (Desc.bBlock2)
                Params->OutTextureBlock2 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture, 0, Desc.UAVFormat));
            else
                Params->OutTextureBlock4 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture, 0, Desc.UAVFormat));

            FComputeShaderUtils::AddPass(
                GraphBuilder,
                FRDGEventName(TEXT("BCGBuffer Encode %s"), Desc.Name),
                Shader,
                Params,
                FComputeShaderUtils::GetGroupCount(BlockCount, kGroupSize));

            return OutputTexture;
        }

        // === Buffer-copy path for non-UAV-aliasing hardware (e.g. Intel) ===
        //
        // Intel does not support UAV format aliasing, so we cannot write BC block data
        // directly through a raw-uint UAV aliased onto a BC-format texture.  Instead:
        //
        //   Step 1: Dispatch compute → write BC blocks to a plain uint texture at
        //           block-count dimensions.  No aliasing is needed because the UAV
        //           format exactly matches the texture format
        //           (R32G32_UINT for BC1/BC4 blocks, R32G32B32A32_UINT for BC3/BC5).
        //
        //   Step 2: Copy uint intermediate → BC texture via a D3D12 format-compatible
        //           copy.  Per the D3D12 format compatibility table:
        //             R32G32_UINT       <-> BC1 / BC4  (both 8 bytes per block)
        //             R32G32B32A32_UINT <-> BC3 / BC5  (both 16 bytes per block)
        //           CopyTextureRegion treats each D3D12 "texel" of a BC texture as one
        //           4×4-pixel block, so a BlockCount×1 uint source maps exactly onto a
        //           SrcExtent BC destination.
        //
        // The lighting pass then samples an actual BC texture → hardware BC decompression
        // → block artifacts are visible exactly as on UAV-aliasing hardware.

        // Step 1a: intermediate uint texture at block-count dimensions (plain UAV, no aliasing).
        const EPixelFormat IntermFmt = Desc.bBlock2 ? PF_R32G32_UINT : PF_R32G32B32A32_UINT;
        const FRDGTextureDesc IntermDesc = FRDGTextureDesc::Create2D(
            BlockCount, IntermFmt, FClearValueBinding::None,
            TexCreate_UAV | TexCreate_NoFastClear);
        FRDGTextureRef IntermTexture = GraphBuilder.CreateTexture(IntermDesc, TEXT("BCGBuffer.UintInterm"));

        // Step 1b: dispatch the same direct encode mode used on the UAV aliasing path.
        //          Each thread writes one uint2/uint4 BC block at position BlockXY.
        FBCGBufferEncodeCS::FPermutationDomain Perm;
        Perm.Set<FBCGBufferEncodeCS::FEncodeModeDim>(Desc.EncodeMode);
        auto Shader = TShaderMapRef<FBCGBufferEncodeCS>(ShaderMap, Perm);

        auto* Params = GraphBuilder.AllocParameters<FBCGBufferEncodeCS::FParameters>();
        Params->SrcTexture       = SrcTexture;
        Params->SrcSize          = SrcExtent;
        Params->BlockCount       = BlockCount;
        Params->OutTextureBlock2 = nullptr;
        Params->OutTextureBlock4 = nullptr;
        Params->OutTextureRGBA   = nullptr;

        if (Desc.bBlock2)
            Params->OutTextureBlock2 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(IntermTexture));
        else
            Params->OutTextureBlock4 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(IntermTexture));

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            FRDGEventName(TEXT("BCGBuffer Encode %s (buf-copy)"), Desc.Name),
            Shader,
            Params,
            FComputeShaderUtils::GetGroupCount(BlockCount, kGroupSize));

        // Step 2a: create output BC texture (shader resource; the copy will fill it).
        const FRDGTextureDesc BCDesc = FRDGTextureDesc::Create2D(
            SrcExtent, Desc.BCFormat, FClearValueBinding::None,
            TexCreate_ShaderResource | TexCreate_NoFastClear);
        FRDGTextureRef BCTexture = GraphBuilder.CreateTexture(BCDesc, Desc.Name);

        // Step 2b: format-compatible subresource copy, uint intermediate → BC texture.
        AddCopyTexturePass(GraphBuilder, IntermTexture, BCTexture);

        return BCTexture;
    }
} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

FBCGBufferTextures AddBCGBufferCompressPass(
    FRDGBuilder&     GraphBuilder,
    const FViewInfo& View,
    FIntPoint        SrcExtent,
    FRDGTextureRef   GBufferC,
    FRDGTextureRef   GBufferA,
    FRDGTextureRef   GBufferB)
{
    FBCGBufferTextures Result;

    RDG_GPU_STAT_SCOPE(GraphBuilder, BCGBufferCompress);
    RDG_EVENT_SCOPE(GraphBuilder, "BCGBuffer Compress");

    const FGlobalShaderMap* ShaderMap = View.ShaderMap;

    static bool bLoggedOnce = false;
    if (!bLoggedOnce)
    {
        bLoggedOnce = true;
        UE_LOG(LogTemp, Warning,
            TEXT("BCGBuffer: GRHISupportsUAVFormatAliasing=%d  path=%s"),
            (int32)GRHISupportsUAVFormatAliasing,
            GRHISupportsUAVFormatAliasing
                ? TEXT("direct BC via UAV aliasing (1 compute pass)")
                : TEXT("buffer-copy: uint intermediate -> BC texture (1 compute + 1 copy)"));

        const int64 OrigPerChannel = (int64)SrcExtent.X * SrcExtent.Y * 4;
        const int64 BC1Bytes = (int64)FMath::DivideAndRoundUp(SrcExtent.X, 4)
                                    * FMath::DivideAndRoundUp(SrcExtent.Y, 4) * 8;
        const int64 BC3Bytes = BC1Bytes * 2;
        const int64 OrigMB   = OrigPerChannel * 3 / (1024 * 1024);
        const int64 BCMB     = (BC1Bytes + BC3Bytes * 2) / (1024 * 1024);
        UE_LOG(LogTemp, Warning,
            TEXT("BCGBuffer VRAM: GBufferA+B+C = %lld MB  ->  BC = %lld MB  (saving %lld MB at %dx%d)"),
            OrigMB, BCMB, OrigMB - BCMB, SrcExtent.X, SrcExtent.Y);
    }

    if (GBufferC) Result.CompressedC = DispatchEncode(GraphBuilder, ShaderMap, GBufferC, SrcExtent, GBCChannelDescs[0]);
    if (GBufferA) Result.CompressedA = DispatchEncode(GraphBuilder, ShaderMap, GBufferA, SrcExtent, GBCChannelDescs[1]);
    if (GBufferB) Result.CompressedB = DispatchEncode(GraphBuilder, ShaderMap, GBufferB, SrcExtent, GBCChannelDescs[2]);

    return Result;
}
