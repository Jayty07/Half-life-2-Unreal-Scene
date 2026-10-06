using UnrealBuildTool;

public class HL2Blockout : ModuleRules
{
	public HL2Blockout(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"PBCharacterMovement"
		});
	}
}
