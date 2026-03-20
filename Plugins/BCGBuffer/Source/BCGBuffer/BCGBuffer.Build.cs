using UnrealBuildTool;

public class BCGBuffer : ModuleRules
{
    public BCGBuffer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "CoreUObject",
            "Engine",
            "Renderer",
            "RenderCore",
            "RHI",
            "Projects",   // IPluginManager
        });

        // Access FSceneTextures and FViewInfo from engine renderer internals
        PrivateIncludePaths.AddRange(new string[]
        {
            EngineDirectory + "/Source/Runtime/Renderer/Private",
            EngineDirectory + "/Source/Runtime/Renderer/Internal",
        });
    }
}
