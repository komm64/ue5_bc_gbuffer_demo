# Engine Patch: SceneTextures.cpp — NOT REQUIRED

**File:** `Engine/Source/Runtime/Renderer/Private/SceneTextures.cpp`
**Engine version:** UE 5.7
**Status:** No changes needed

---

## Why no patch is needed

UE 5.7 removed `ETextureCreateFlags::Transient` from the enum (the `#define` shell remains but
the enum member does not exist — it will not compile).

More importantly, the VRAM savings already occur automatically through the **RDG transient
allocator** without any engine change:

1. `BCGBuffer_SubstituteCallback` replaces `SceneTextures.GBufferA/B/C` with compressed versions.
2. The SceneTextures uniform buffer is rebuilt immediately after substitution (see
   `BasePassRendering.cpp` patch), pointing all subsequent shader parameters at the new textures.
3. Because no further RDG pass references the original GBuffer textures, RDG marks them dead
   and the transient allocator can reuse their backing memory within the same frame.

---

## Enabling the transient allocator

```
r.RDG.TransientAllocator 1
```

This CVar (default 0 in editor builds) enables within-frame memory aliasing.  With it enabled,
the three original GBuffers (~7.9 MB each at 1080p, ~32 MB each at 4K) become eligible for
memory reuse after the BC compression pass reads them.

---

## Measuring VRAM savings

```
stat RHI          → compare RenderTargetMemory2D with r.BCGBuffer.Enable 0 vs 1
r.RDG.Debug 1     → verbose transient allocator log (shows aliased textures)
```

**Expected savings (1080p, RGBA8 GBuffers):**
- GBufferA: 1920×1080×4 B = ~7.9 MB
- GBufferB: 1920×1080×4 B = ~7.9 MB
- GBufferC: 1920×1080×4 B = ~7.9 MB
- **Total: ~24 MB freed per frame at 1080p, ~96 MB at 4K**
