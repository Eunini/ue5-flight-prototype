using UnrealBuildTool;

public class FlightProtoEditorTarget : TargetRules
{
	public FlightProtoEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_4;
		ExtraModuleNames.Add("FlightProto");
	}
}
