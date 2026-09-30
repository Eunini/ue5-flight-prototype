using UnrealBuildTool;

public class FlightProtoEditorTarget : TargetRules
{
	public FlightProtoEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("FlightProto");
	}
}
