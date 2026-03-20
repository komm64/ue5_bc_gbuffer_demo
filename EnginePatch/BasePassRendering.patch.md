# Engine Patch: BasePassRendering.cpp

**File:** `Engine/Source/Runtime/Renderer/Private/BasePassRendering.cpp`
**Engine version:** UE 5.7
**Purpose:** Function-pointer hook that allows the BCGBuffer plugin to substitute
G-Buffer textures with BC-compressed versions after the Base Pass, before the
Lighting Pass runs.

Using a raw function pointer avoids virtual-dispatch vtable mismatches that occur
when a plugin's override of `FSceneViewExtension` virtual methods is called by a
binary engine build that was compiled with a different vtable layout.

---

## Change 1 — Top of file (after copyright, before first #include)

```cpp
// BCGBuffer: function-pointer hook for G-Buffer texture substitution after Base Pass.
// Using a raw function pointer avoids virtual-dispatch vtable mismatches with binary engine installs.
// The BCGBuffer plugin registers its callback at startup via RegisterBCGBufferSubstituteCallback().
#include "SceneTextures.h"
RENDERER_API void (*GBCGBuffer_SubstituteCallback)(FRDGBuilder& GraphBuilder, const FSceneView& View, FSceneTextures& SceneTextures) = nullptr;
RENDERER_API void RegisterBCGBufferSubstituteCallback(void (*Callback)(FRDGBuilder&, const FSceneView&, FSceneTextures&))
{
	GBCGBuffer_SubstituteCallback = Callback;
}
```

---

## Change 2 — Inside RenderBasePass(), after the ViewExtension loop (≈ line 1313)

Find the closing brace of the ViewExtension PostRenderBasePassDeferred loop:
```cpp
	// (existing code)
	for (FViewExtensionRef& ViewExtension : ActiveExtensions)
	{
		ViewExtension->PostRenderBasePassDeferred_RenderThread(...);
	}
	// ← INSERT HERE
```

Insert:
```cpp
	// BCGBuffer: allow registered plugin to substitute G-Buffer textures before the Lighting Pass.
	// Uses a raw function pointer to avoid virtual-dispatch vtable issues with binary engine installs.
	if (GBCGBuffer_SubstituteCallback)
	{
		const FRDGTextureRef PrevA = SceneTextures.GBufferA;
		const FRDGTextureRef PrevB = SceneTextures.GBufferB;
		const FRDGTextureRef PrevC = SceneTextures.GBufferC;

		for (FViewInfo& View : InViews)
		{
			GBCGBuffer_SubstituteCallback(GraphBuilder, View, SceneTextures);
		}

		if (SceneTextures.GBufferA != PrevA || SceneTextures.GBufferB != PrevB || SceneTextures.GBufferC != PrevC)
		{
			SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, Renderer.FeatureLevel, SceneTextures.SetupMode);
		}
	}
```

---

## Rebuild

After applying, recompile the Renderer module and regenerate the import lib:

```powershell
# Recompile relevant Unity build files:
# Module.Renderer.1.cpp  (contains BasePassRendering.cpp)
# Module.Renderer.15.cpp (or whichever unity file contains it)
# Then relink UnrealEditor-Renderer.dll

# Regenerate import lib (needed because new RENDERER_API symbols were added):
# C:/Temp/regen_renderer_lib.ps1
```
