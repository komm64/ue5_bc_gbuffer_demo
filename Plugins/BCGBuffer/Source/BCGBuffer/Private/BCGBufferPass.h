#pragma once

#include "RenderGraph.h"

class FViewInfo;

// Output textures produced by the BC G-Buffer compression pass.
//
// On hardware that supports UAV format aliasing (GRHISupportsUAVFormatAliasing),
// each texture is a proper BC-format SRV at full G-Buffer resolution; the hardware
// texture sampler decodes them transparently.
//
// On hardware without UAV format aliasing (e.g. Intel integrated graphics),
// the textures are PF_FloatRGBA round-trip results: the data was BC-encoded and
// immediately decoded back to float4.  No memory savings, but the visual quality
// impact of BC compression is still demonstrable.
struct FBCGBufferTextures
{
    FRDGTextureRef CompressedC = nullptr;  // GBufferC (BaseColor + AO) — BC1 or float4 round-trip
    FRDGTextureRef CompressedA = nullptr;  // GBufferA (Normal + PerObjectData) — BC3 or float4 round-trip
    FRDGTextureRef CompressedB = nullptr;  // GBufferB (ORM + ShadingModelID) — BC3 or float4 round-trip

    bool IsValid() const { return CompressedC || CompressedA || CompressedB; }
};

// Returns true when r.BCGBuffer.Enable != 0 (render-thread safe).
bool IsBCGBufferEnabled();

// Inserts BC compression compute passes for the supplied G-Buffer textures.
//
// SrcExtent – full-resolution pixel size of the G-Buffer.
// GBufferC/A/B must be valid FRDGTextureRef from the current RDG graph.
FBCGBufferTextures AddBCGBufferCompressPass(
    FRDGBuilder&     GraphBuilder,
    const FViewInfo& View,
    FIntPoint        SrcExtent,
    FRDGTextureRef   GBufferC,   // BaseColor (RGB) + AO (A)
    FRDGTextureRef   GBufferA,   // Normal (oct-encoded RG) + PerObjectData (BA)
    FRDGTextureRef   GBufferB);  // Roughness/Metallic/Specular (RGB) + ShadingModelID (A)
