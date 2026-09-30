using UnrealBuildTool;

public class FlightProto : ModuleRules
{
	public FlightProto(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "NetCore" });
		PublicIncludePaths.Add(ModuleDirectory);
	}
}
