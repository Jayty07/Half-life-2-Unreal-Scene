using UnrealBuildTool;

public class HL2BlockoutTarget : TargetRules
{
	public HL2BlockoutTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("HL2Blockout");
	}
}
