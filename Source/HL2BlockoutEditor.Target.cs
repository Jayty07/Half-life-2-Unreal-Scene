using UnrealBuildTool;

public class HL2BlockoutEditorTarget : TargetRules
{
	public HL2BlockoutEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("HL2Blockout");
	}
}
