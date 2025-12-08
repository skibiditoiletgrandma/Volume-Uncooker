using UnrealBuildTool;

public class VolumeClipboard : ModuleRules
{
    public VolumeClipboard(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
            }
            );


        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Projects",
                "InputCore",
                "UnrealEd",
                "ToolMenus",
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "Json",
                "JsonUtilities",
                "ApplicationCore",
                "EditorStyle"    // <--- ADD THIS LINE (Don't forget the comma above it)
			}
            );
    }
}