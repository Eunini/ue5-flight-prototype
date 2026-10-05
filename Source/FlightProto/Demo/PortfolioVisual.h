#pragma once
#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

namespace PortfolioVisual
{
 inline UMaterialInstanceDynamic* Color(UStaticMeshComponent* Part, const FLinearColor& Tint)
 {
  UMaterialInterface* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Portfolio/M_Surface.M_Surface"));
  if (!Base) Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
  auto* Material=UMaterialInstanceDynamic::Create(Base,Part);
  if (!Material) return nullptr;
  Material->SetVectorParameterValue(TEXT("Tint"),Tint);
  Material->SetVectorParameterValue(TEXT("Color"),Tint);
  Part->SetMaterial(0,Material);
  return Material;
 }
 inline UStaticMeshComponent* Part(AActor* Owner, FVector Location, FVector Size, FLinearColor Tint,
                                  bool Solid=true, const TCHAR* Shape=TEXT("Cube"))
 {
  auto* Mesh=NewObject<UStaticMeshComponent>(Owner);
  Owner->AddInstanceComponent(Mesh);
  Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"),Shape,Shape)));
  Mesh->SetupAttachment(Owner->GetRootComponent());
  Mesh->SetRelativeLocation(Location);
  Mesh->SetRelativeScale3D(Size/100.f);
  Mesh->SetCollisionProfileName(Solid?TEXT("BlockAll"):TEXT("NoCollision"));
  Color(Mesh,Tint);
  Mesh->RegisterComponent();
  return Mesh;
 }
 inline void Clear(AActor* Owner)
 {
  TArray<UActorComponent*> Parts; Owner->GetComponents(Parts);
  for (UActorComponent* Part:Parts)
   if (Part->CreationMethod==EComponentCreationMethod::Instance) Part->DestroyComponent();
 }
 inline void Label(AActor* Owner, const FString& Text, FVector Location, float Size=55.f)
 {
  auto* Label=NewObject<UTextRenderComponent>(Owner);
  Owner->AddInstanceComponent(Label);
  Label->SetupAttachment(Owner->GetRootComponent());
  Label->SetRelativeLocation(Location);
  Label->SetRelativeRotation(FRotator(0,180,0));
  Label->SetText(FText::FromString(Text));
  Label->SetWorldSize(Size);
  Label->SetHorizontalAlignment(EHTA_Center);
  Label->SetTextRenderColor(FColor(217,235,236));
  Label->RegisterComponent();
 }
 inline void Environment(AActor* Owner)
 {
  auto* Sun=NewObject<UDirectionalLightComponent>(Owner);
  Owner->AddInstanceComponent(Sun); Sun->SetupAttachment(Owner->GetRootComponent());
  Sun->SetMobility(EComponentMobility::Movable); Sun->SetRelativeRotation(FRotator(-35,-25,0)); Sun->SetIntensity(4.f);
  Sun->SetLightColor(FLinearColor(1.f,.86f,.68f)); Sun->RegisterComponent();
  auto* Sky=NewObject<USkyLightComponent>(Owner);
  Owner->AddInstanceComponent(Sky); Sky->SetupAttachment(Owner->GetRootComponent());
  Sky->SetMobility(EComponentMobility::Movable); Sky->bRealTimeCapture=false; Sky->SetIntensity(.8f); Sky->RegisterComponent();
  auto* Atmosphere=NewObject<USkyAtmosphereComponent>(Owner);
  Owner->AddInstanceComponent(Atmosphere); Atmosphere->SetupAttachment(Owner->GetRootComponent()); Atmosphere->RegisterComponent();
  auto* Fog=NewObject<UExponentialHeightFogComponent>(Owner);
  Owner->AddInstanceComponent(Fog); Fog->SetupAttachment(Owner->GetRootComponent());
  Fog->SetFogDensity(.003f); Fog->RegisterComponent();
 }
}
