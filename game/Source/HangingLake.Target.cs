using UnrealBuildTool;
public class HangingLakeTarget : TargetRules
{
	public HangingLakeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("HangingLake");
	}
}
