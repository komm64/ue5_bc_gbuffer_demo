#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"
#include "RHI.h"
#include "Windows/WindowsHWrapper.h"  // GetAsyncKeyState

// ---------------------------------------------------------------------------
// Polls B key each frame and shows a persistent HUD:
//   BCGBuffer: ON/OFF   FPS: xxx   CPU: x.xms   GPU: x.xms
// No Slate / GameMode / Blueprint dependency.
// ---------------------------------------------------------------------------

class FBCGBufferKeyPoller : public FTickableGameObject
{
public:
    virtual void Tick(float DeltaTime) override
    {
        // --- B key toggle ---
        const bool bDown = (::GetAsyncKeyState('B') & 0x8000) != 0;
        if (bDown && !bWasDownLastFrame)
        {
            IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BCGBuffer.Enable"));
            if (CVar)
            {
                const int32 NewVal = CVar->GetInt() ? 0 : 1;
                CVar->Set(NewVal, ECVF_SetByConsole);
            }
        }
        bWasDownLastFrame = bDown;

        // --- Persistent HUD (updated every frame) ---
        if (!GEngine) return;

        IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BCGBuffer.Enable"));
        const bool bEnabled = CVar && CVar->GetInt() != 0;

        const float FPS      = 1.0f / FMath::Max(DeltaTime, SMALL_NUMBER);
        const float CPUms    = DeltaTime * 1000.0f;
        const float GPUms    = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());

        const FString Line = FString::Printf(
            TEXT("BCGBuffer: %s   FPS: %.1f   CPU: %.2f ms   GPU: %.2f ms   [B = toggle]"),
            bEnabled ? TEXT("ON ") : TEXT("OFF"),
            FPS, CPUms, GPUms);

        GEngine->AddOnScreenDebugMessage(
            /*Key=*/42,
            /*TimeToDisplay=*/0.0f,   // 0 = display for exactly one frame (refreshed each tick)
            bEnabled ? FColor::Green : FColor::Silver,
            Line,
            /*bNewerOnTop=*/true,
            FVector2D(1.5f, 1.5f));   // slightly larger text
    }

    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(FBCGBufferKeyPoller, STATGROUP_Tickables);
    }

    virtual bool IsTickableInEditor() const override { return false; }
    virtual bool IsTickableWhenPaused() const override { return true; }

private:
    bool bWasDownLastFrame = false;
};
