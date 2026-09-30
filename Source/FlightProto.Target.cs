using UnrealBuildTool;

public class FlightProtoTarget : TargetRules
{
	public FlightProtoTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("FlightProto");
	}
}
