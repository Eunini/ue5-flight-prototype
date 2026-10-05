using UnrealBuildTool;
public class PortfolioForge : ModuleRules
{
 public PortfolioForge(ReadOnlyTargetRules Target) : base(Target)
 {
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  PrivateDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","UnrealEd","Kismet",
   "KismetCompiler","BlueprintGraph","AssetRegistry","Json","MaterialEditor"});
 }
}
