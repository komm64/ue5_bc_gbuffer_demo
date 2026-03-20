# Engine Patch: DeferredShadingRenderer.cpp

File: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp`

## Required change

Insert the BC compression pass immediately after `AddResolveSceneDepthPass`,
before Nanite visualization (around line 2911 in UE 5.7).

### Add include near top of file

```cpp
// Near other Renderer includes
#include "BCGBufferPass.h"   // BCGBuffer plugin
```

### Add pass call after depth resolve

```cpp
    RenderBasePass(/*...*/);                               // line ~2905

    if (!bAllowReadOnlyDepthBasePass)
    {
        AddResolveSceneDepthPass(GraphBuilder, Views, SceneTextures.Depth);
    }

    // ---- BC G-Buffer compression ----------------------------------------
    // Compress G-Buffer render targets into BC block data immediately after
    // the Base Pass.  The compressed textures are smaller and reduce the
    // memory bandwidth consumed by the Lighting Pass.
    //
    // TODO: Pass BCTextures into the Deferred Lighting inputs to complete
    // the bandwidth reduction (currently the compression pass runs but
    // the Lighting Pass still reads the original uncompressed G-Buffer).
    FBCGBufferTextures BCTextures;
    if (CVarBCGBufferEnable.GetValueOnRenderThread() != 0)
    {
        BCTextures = AddBCGBufferCompressPass(GraphBuilder, Views[0], SceneTextures);
    }
    // ---------------------------------------------------------------------

    if (bNaniteEnabled)                                    // line ~2913
    {
        // ...
    }
```

## Console variable (add to BCGBufferPass.cpp)

```cpp
static TAutoConsoleVariable<int32> CVarBCGBufferEnable(
    TEXT("r.BCGBuffer.Enable"),
    0,
    TEXT("0=off  1=compress G-Buffer to BC after Base Pass"),
    ECVF_RenderThreadSafe);
```

## Next: hook BCTextures into Lighting Pass

The Deferred Lighting inputs are assembled in
`FDeferredShadingSceneRenderer::RenderDeferredLighting()`.
Binding `BCTextures.NormalBC5` etc. in place of the original G-Buffer SRVs
is the next step to actually measure bandwidth reduction.

Note: For hardware BC decode in the Lighting Pass shaders, the output
textures need to be aliased to typed BC SRVs (BC1/BC5/BC4 formats).
This requires `TexCreate_UAV` on a typeless BC texture — see DX12
`DXGI_FORMAT_*_TYPELESS` + UAV format casting.
