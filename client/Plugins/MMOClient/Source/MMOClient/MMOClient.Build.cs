using System;
using System.IO;
using UnrealBuildTool;

public class MMOClient : ModuleRules
{
    public MMOClient(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Sockets", "Networking", "UMG" });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "OpenSSL"
        });
        // No need for custom LZ4 integration; use Unreal's built-in LZ4 support.
    }
}
