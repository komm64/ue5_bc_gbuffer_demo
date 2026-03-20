#include "BCGBufferModule.h"
#include "BCGBufferViewExtension.h"
#include "BCGBufferInputProcessor.h"
#include "Tickable.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "Modules/ModuleManager.h"
#include "SceneViewExtension.h"
#include "Misc/CoreDelegates.h"

DEFINE_LOG_CATEGORY_STATIC(LogBCGBufferModule, Log, All);

static TSharedPtr<FBCGBufferViewExtension, ESPMode::ThreadSafe> GBCGBufferViewExtension;
static TUniquePtr<FBCGBufferKeyPoller> GBCGBufferKeyPoller;

void FBCGBufferModule::StartupModule()
{
    // Map /Plugin/BCGBuffer/ -> Plugins/BCGBuffer/Shaders/
    // Must happen at PostConfigInit so shaders are found during compilation.
    FString PluginShaderDir = FPaths::Combine(
        IPluginManager::Get().FindPlugin(TEXT("BCGBuffer"))->GetBaseDir(),
        TEXT("Shaders"));
    AddShaderSourceDirectoryMapping(TEXT("/Plugin/BCGBuffer"), PluginShaderDir);

    // NewExtension requires GEngine — defer until after engine init.
    FCoreDelegates::OnPostEngineInit.AddLambda([]()
    {
        UE_LOG(LogBCGBufferModule, Warning, TEXT("OnPostEngineInit: GEngine=%p, ViewExtensions=%p"),
            (void*)GEngine,
            GEngine ? (void*)GEngine->ViewExtensions.Get() : nullptr);

        // Count BEFORE registration
        int32 CountBefore = 0;
        if (GEngine && GEngine->ViewExtensions)
        {
            FSceneViewExtensionContext NullCtx((FViewport*)nullptr);
            CountBefore = GEngine->ViewExtensions->GatherActiveExtensions(NullCtx).Num();
        }
        UE_LOG(LogBCGBufferModule, Warning, TEXT("Before NewExtension: active count=%d"), CountBefore);

        GBCGBufferViewExtension = FSceneViewExtensions::NewExtension<FBCGBufferViewExtension>();
        UE_LOG(LogBCGBufferModule, Warning, TEXT("OnPostEngineInit: extension created, IsValid=%d, Ptr=%p"),
            GBCGBufferViewExtension.IsValid() ? 1 : 0,
            GBCGBufferViewExtension.Get());

        // Register function-pointer hook for G-Buffer texture substitution (Step 2).
        BCGBuffer_RegisterSubstituteCallback();

        // Register key poller: B key toggles r.BCGBuffer.Enable
        GBCGBufferKeyPoller = MakeUnique<FBCGBufferKeyPoller>();
        UE_LOG(LogBCGBufferModule, Warning, TEXT("BCGBuffer key poller registered (B key = toggle)"));

        // Direct virtual dispatch test — proves vtable is correct
        if (GBCGBufferViewExtension.IsValid())
        {
            FSceneViewExtensionContext NullCtx((FViewport*)nullptr);
            bool bDirect = GBCGBufferViewExtension->IsActiveThisFrame(NullCtx);
            UE_LOG(LogBCGBufferModule, Warning,
                TEXT("Direct IsActiveThisFrame call returned: %d"), bDirect ? 1 : 0);
        }

        // Count AFTER registration — should be CountBefore + 1 if registration succeeded
        if (GEngine && GEngine->ViewExtensions)
        {
            FSceneViewExtensionContext NullCtx((FViewport*)nullptr);
            TArray<FSceneViewExtensionRef> Active = GEngine->ViewExtensions->GatherActiveExtensions(NullCtx);
            UE_LOG(LogBCGBufferModule, Warning,
                TEXT("After NewExtension: active count=%d (ViewExtensions=%p)"),
                Active.Num(), (void*)GEngine->ViewExtensions.Get());
        }
    });
}

void FBCGBufferModule::ShutdownModule()
{
    GBCGBufferKeyPoller.Reset();
    GBCGBufferViewExtension.Reset();
}

IMPLEMENT_MODULE(FBCGBufferModule, BCGBuffer)
