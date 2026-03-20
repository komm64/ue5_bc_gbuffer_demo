# apply_patch.ps1
# Applies BCGBuffer plugin hooks to UE5 engine source files.
# Run this script as Administrator:
#   Right-click -> "Run with PowerShell as Administrator"
# OR from an elevated PowerShell terminal:
#   Set-ExecutionPolicy Bypass -Scope Process
#   & "C:\Users\komm64\Projects\ue5_bc_gbuffer_demo\EnginePatch\apply_patch.ps1"

$EngineRoot = "C:\Program Files\Epic Games\UE_5.7\Engine\Source\Runtime"

$Files = @{
    ViewExtension  = "$EngineRoot\Engine\Public\SceneViewExtension.h"
    BasePass       = "$EngineRoot\Renderer\Private\BasePassRendering.cpp"
}

foreach ($f in $Files.Values) {
    if (-not (Test-Path $f)) {
        Write-Error "File not found: $f"
        exit 1
    }
}

# ============================================================
# Patch 1: SceneViewExtension.h
#   Add forward declaration of FSceneTextures and a new virtual
#   method PostBasePassSubstituteSceneTextures_RenderThread.
#   This gives ViewExtensions a chance to swap G-Buffer SRVs
#   with BC-compressed versions before the Lighting Pass.
# ============================================================

$Content = Get-Content $Files.ViewExtension -Raw

$Marker1 = "PostRenderBasePassDeferred_RenderThread"

if ($Content -like "*PostBasePassSubstituteSceneTextures*") {
    Write-Host "[SKIP] Patch 1a already applied."
} elseif (-not ($Content -like "*$Marker1*")) {
    Write-Error "Patch 1: marker '$Marker1' not found in SceneViewExtension.h"
    exit 1
} else {
    # Add forward declaration before the class body
    $OldFwd = "#pragma once"
    $NewFwd  = "#pragma once`r`n`r`nstruct FSceneTextures; // forward declaration for BCGBuffer substitution hook"
    $Content = $Content.Replace($OldFwd, $NewFwd)

    # Add new method right after PostRenderBasePassDeferred_RenderThread
    $OldMethod = @'
	virtual void PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) {}
'@
    $NewMethod = @'
	virtual void PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) {}

	/**
	 * Called immediately after PostRenderBasePassDeferred_RenderThread for all extensions.
	 * Allows a ViewExtension to substitute G-Buffer textures (e.g., BC-compressed versions)
	 * into FSceneTextures before the Lighting Pass reads them.
	 * The SceneTextures.UniformBuffer is automatically rebuilt if any texture ref is changed.
	 */
	virtual void PostBasePassSubstituteSceneTextures_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, FSceneTextures& SceneTextures) {}
'@
    $Content = $Content.Replace($OldMethod, $NewMethod)
    Write-Host "[OK]   Patch 1 applied: added PostBasePassSubstituteSceneTextures_RenderThread to ISceneViewExtension."
}

$Content | Set-Content $Files.ViewExtension -NoNewline

# ============================================================
# Patch 2: BasePassRendering.cpp
#   After the existing PostRenderBasePassDeferred loop, add a
#   second loop that calls PostBasePassSubstituteSceneTextures.
#   If any G-Buffer ref was replaced, rebuild the UniformBuffer.
# ============================================================

$Content = Get-Content $Files.BasePass -Raw

if ($Content -like "*PostBasePassSubstituteSceneTextures*") {
    Write-Host "[SKIP] Patch 2 already applied."
} else {
    $OldLoop = @'
	for (const TSharedRef<ISceneViewExtension>& ViewExtension : ViewFamily.ViewExtensions)
	{
		for (FViewInfo& View : InViews)
		{
			ViewExtension->PostRenderBasePassDeferred_RenderThread(GraphBuilder, View, BasePassRenderTargets, SceneTextures.UniformBuffer);
		}
	}
'@
    $NewLoop = @'
	for (const TSharedRef<ISceneViewExtension>& ViewExtension : ViewFamily.ViewExtensions)
	{
		for (FViewInfo& View : InViews)
		{
			ViewExtension->PostRenderBasePassDeferred_RenderThread(GraphBuilder, View, BasePassRenderTargets, SceneTextures.UniformBuffer);
		}
	}

	// BCGBuffer: allow ViewExtensions to substitute G-Buffer textures (e.g., BC-compressed)
	// before the Lighting Pass.  If any ref changes, the UniformBuffer is rebuilt.
	{
		FRDGTextureRef PrevA = SceneTextures.GBufferA;
		FRDGTextureRef PrevB = SceneTextures.GBufferB;
		FRDGTextureRef PrevC = SceneTextures.GBufferC;

		for (const TSharedRef<ISceneViewExtension>& ViewExtension : ViewFamily.ViewExtensions)
		{
			for (FViewInfo& View : InViews)
			{
				ViewExtension->PostBasePassSubstituteSceneTextures_RenderThread(GraphBuilder, View, SceneTextures);
			}
		}

		if (SceneTextures.GBufferA != PrevA || SceneTextures.GBufferB != PrevB || SceneTextures.GBufferC != PrevC)
		{
			SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, Renderer.FeatureLevel, SceneTextures.SetupMode);
		}
	}
'@

    if (-not ($Content -like "*PostRenderBasePassDeferred_RenderThread*")) {
        Write-Error "Patch 2: insertion point not found. Engine version mismatch?"
        exit 1
    }

    $Content = $Content.Replace($OldLoop, $NewLoop)
    Write-Host "[OK]   Patch 2 applied: added PostBasePassSubstituteSceneTextures call in BasePassRendering.cpp."
}

$Content | Set-Content $Files.BasePass -NoNewline

Write-Host ""
Write-Host "All patches applied. Rebuild the Renderer module:"
Write-Host "  Open the .uproject in UE5, or run:"
Write-Host "  Build.bat ue5_bc_gbuffer_demoEditor Win64 Development <path>.uproject"
