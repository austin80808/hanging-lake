using UnrealBuildTool;

public class HangingLake : ModuleRules
{
	public HangingLake(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"UMG", "Slate", "SlateCore", "RenderCore", "ProceduralMeshComponent",
			"Json"
		});
	}
}
