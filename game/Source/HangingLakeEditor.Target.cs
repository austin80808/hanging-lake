using UnrealBuildTool;
public class HangingLakeEditorTarget : TargetRules
{
	public HangingLakeEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("HangingLake");
	}
}
