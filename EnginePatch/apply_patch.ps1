# apply_patch.ps1
# Applies the BCGBuffer function-pointer hook to BasePassRendering.cpp.
#
# Usage (from an elevated PowerShell terminal):
#   Set-ExecutionPolicy Bypass -Scope Process
#   & "<path-to-repo>\EnginePatch\apply_patch.ps1"
#
# Or just double-click apply_patch_asadmin.bat (auto-elevates).
#
# Parameters:
#   -EngineRoot  Path to the UE engine root (default: UE 5.7 default install)

param(
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.7"
)

$TargetFile = "$EngineRoot\Engine\Source\Runtime\Renderer\Private\BasePassRendering.cpp"

if (-not (Test-Path $TargetFile)) {
    Write-Error "File not found: $TargetFile`nUse -EngineRoot to specify the correct engine path."
    exit 1
}

$Content = Get-Content $TargetFile -Raw

# Already patched?
if ($Content -like "*GBCGBuffer_SubstituteCallback*") {
    Write-Host "[SKIP] Patch already applied to $TargetFile"
    exit 0
}

# ------------------------------------------------------------------
# Patch 1: inject function-pointer declaration at the top of the file.
# Inserted between the copyright line and the first #include.
# ------------------------------------------------------------------
$Marker1 = '#include "BasePassRendering.h"'

if (-not ($Content -like "*$Marker1*")) {
    Write-Error "Patch 1: marker not found. Engine version mismatch?"
    exit 1
}

$Injection1 = @'
#include "SceneTextures.h"
void (*GBCGBuffer_SubstituteCallback)(FRDGBuilder& GraphBuilder, const FSceneView& View, FSceneTextures& SceneTextures) = nullptr;
void RegisterBCGBufferSubstituteCallback(void (*Callback)(FRDGBuilder&, const FSceneView&, FSceneTextures&))
{
	GBCGBuffer_SubstituteCallback = Callback;
}

'@

$Content = $Content.Replace($Marker1, $Injection1 + $Marker1)

# ------------------------------------------------------------------
# Patch 2: inject callback invocation after the Base Pass completes.
# Inserted just before the bRequiresFarZQuadClear block.
# ------------------------------------------------------------------
$Marker2 = "`tif (bRequiresFarZQuadClear)"

if (-not ($Content -like "*bRequiresFarZQuadClear*")) {
    Write-Error "Patch 2: marker not found. Engine version mismatch?"
    exit 1
}

$Injection2 = @'
	if (GBCGBuffer_SubstituteCallback)
	{
		const FRDGTextureRef PrevA = SceneTextures.GBufferA;
		const FRDGTextureRef PrevB = SceneTextures.GBufferB;
		const FRDGTextureRef PrevC = SceneTextures.GBufferC;
		for (FViewInfo& View : InViews)
			GBCGBuffer_SubstituteCallback(GraphBuilder, View, SceneTextures);
		if (SceneTextures.GBufferA != PrevA || SceneTextures.GBufferB != PrevB || SceneTextures.GBufferC != PrevC)
			SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, Renderer.FeatureLevel, SceneTextures.SetupMode);
	}

'@

# Replace only the first occurrence
$idx = $Content.IndexOf($Marker2)
if ($idx -lt 0) {
    Write-Error "Patch 2: marker string not found."
    exit 1
}
$Content = $Content.Substring(0, $idx) + $Injection2 + $Content.Substring($idx)

# ------------------------------------------------------------------
# Write result
# ------------------------------------------------------------------
$Content | Set-Content $TargetFile -NoNewline -Encoding UTF8

Write-Host "[OK] Patch applied to $TargetFile"
Write-Host "     Recompile the Renderer module before running UE5."
Write-Host "     See EnginePatch/BasePassRendering.patch.md for rebuild instructions."
