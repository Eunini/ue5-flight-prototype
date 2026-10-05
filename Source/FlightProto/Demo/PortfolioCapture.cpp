#include "PortfolioCapture.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "UnrealClient.h"
#include "UObject/Class.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
#include "Aircraft/FlightPawn.h"
#include "Systems/AircraftSystemsComponent.h"
#include "Kismet/GameplayStatics.h"

void UFlightCaptureSubsystem::Tick(float Delta)
{
 const bool Verify=FParse::Param(FCommandLine::Get(),TEXT("PortfolioVerify"));
 if (!GetWorld()->IsGameWorld() || (!Verify && !FParse::Param(FCommandLine::Get(),TEXT("PortfolioCapture"))) || bFinished) return;
 if (!Verify && (!GEngine || !GEngine->GameViewport)) return;
 if (!bConfigured)
 {
  Limit=1350;FParse::Value(FCommandLine::Get(),TEXT("PortfolioFrames="),Limit);Limit=FMath::Clamp(Limit,30,3600);
  Directory=FPaths::ProjectSavedDir()/TEXT("PortfolioFrames");
  IFileManager::Get().MakeDirectory(*Directory,true);
  FApp::SetUseFixedTimeStep(true);FApp::SetFixedDeltaTime(1.0/30.0);
  if (!Verify) Handle=UGameViewportClient::OnScreenshotCaptured().AddUObject(this,&UFlightCaptureSubsystem::Captured);
  bConfigured=true;
 }
#if WITH_EDITOR
 if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) {Warmup=0;return;}
#endif
 if (++Warmup<=30 || bQueued) return;
 if (Verify) {if (++Frame>=Limit) FinishCapture(0,0);return;}
 bQueued=true;FScreenshotRequest::RequestScreenshot(TEXT("PortfolioFrame"),true,false);
}
void UFlightCaptureSubsystem::Captured(int32 Width,int32 Height,const TArray<FColor>& Colors)
{
 if (bFinished || !bQueued) return;
 bQueued=false;
 TArray64<uint8> PNG;
 FImageUtils::PNGCompressImageArray(Width,Height,TArrayView64<const FColor>(Colors.GetData(),Colors.Num()),PNG);
 const FString Name=Directory/FString::Printf(TEXT("frame-%05d.png"),Frame);
 if (PNG.IsEmpty() || !FFileHelper::SaveArrayToFile(PNG,*Name))
 {bFinished=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}
 ++Frame;
 if (Frame>=Limit) FinishCapture(Width,Height);
}
void UFlightCaptureSubsystem::FinishCapture(int32 Width,int32 Height)
 {
  const auto* Pilot=Cast<AFlightPawn>(UGameplayStatics::GetPlayerPawn(GetWorld(),0));
  const bool Complete=Pilot && Pilot->GetClass()->GetName()==TEXT("BP_Aircraft_C") && Pilot->IsAirborne() && Pilot->GetAltitudeMetres()>25.f && Pilot->GetTelemetry().Airspeed>24. &&
      Pilot->GetSystems()->GetEngineState()==EEngineState::Running;
  if (!Complete)
  {
   UE_LOG(LogTemp,Error,TEXT("Alpine Flight objectives incomplete: altitude=%.2f speed=%.2f engine=%d"),
      Pilot?Pilot->GetAltitudeMetres():-1.f,Pilot?Pilot->GetTelemetry().Airspeed:-1.,
      Pilot?static_cast<int32>(Pilot->GetSystems()->GetEngineState()):-1);
   bFinished=true;FPlatformMisc::RequestExitWithStatus(false,2);return;
  }
  const FString Evidence=FString::Printf(TEXT("{\"success\":true,\"blueprintClass\":\"BP_Aircraft_C\",\"engineStarted\":true,\"airborne\":true,\"altitudeMetres\":%.2f,\"airspeedMetresPerSecond\":%.2f}"),Pilot->GetAltitudeMetres(),Pilot->GetTelemetry().Airspeed);
  FFileHelper::SaveStringToFile(Evidence,*(FPaths::ProjectSavedDir()/TEXT("GameplayEvidence.json")));
  if (Width>0 && Height>0)
  {
  const FString Receipt=FString::Printf(TEXT("{\"success\":true,\"frames\":%d,\"width\":%d,\"height\":%d,\"fps\":30,\"renderer\":\"Unreal Engine 5.4\"}"),Frame,Width,Height);
  FFileHelper::SaveStringToFile(Receipt,*(FPaths::ProjectSavedDir()/TEXT("PortfolioCapture.json")));
  }
  bFinished=true;FPlatformMisc::RequestExit(false);
}
void UFlightCaptureSubsystem::Deinitialize()
{
 if (Handle.IsValid()) UGameViewportClient::OnScreenshotCaptured().Remove(Handle);
 if (bConfigured) FApp::SetUseFixedTimeStep(false);
 Super::Deinitialize();
}
