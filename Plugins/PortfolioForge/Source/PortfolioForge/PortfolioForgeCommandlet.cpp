#include "PortfolioForgeCommandlet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraphSchema_K2.h"
#include "FileHelpers.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
 bool SaveAsset(UObject* Object, const FString& Path)
 {
  Object->MarkPackageDirty();
  FSavePackageArgs Args;
  Args.TopLevelFlags=RF_Public|RF_Standalone;
  const FString Filename=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());
  IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
  return UPackage::SavePackage(Object->GetOutermost(),Object,*Filename,Args);
 }
 bool ApplyDefaults(UObject* Object, const TSharedPtr<FJsonObject>& Spec)
 {
  const TSharedPtr<FJsonObject>* Defaults;
  if (!Spec->TryGetObjectField(TEXT("defaults"),Defaults)) return true;
  for (const auto& Pair:(*Defaults)->Values)
  {
   FProperty* Property=Object->GetClass()->FindPropertyByName(FName(*Pair.Key));
   FString Value;
   if (!Property || !Pair.Value->TryGetString(Value) ||
       !Property->ImportText_Direct(*Value,Property->ContainerPtrToValuePtr<void>(Object),Object,PPF_None)) return false;
  }
  return true;
 }
 UBlueprint* BuildGraph(const TSharedPtr<FJsonObject>& Spec)
 {
  const FString Path=Spec->GetStringField(TEXT("path"));
  UClass* Parent=LoadClass<UObject>(nullptr,*Spec->GetStringField(TEXT("parent")));
  if (!Parent) return nullptr;
  UPackage* Package=CreatePackage(*Path);
  UBlueprint* BP=FindObject<UBlueprint>(Package,*FPackageName::GetLongPackageAssetName(Path));
  if (!BP)
  {
   BP=FKismetEditorUtilities::CreateBlueprint(Parent,Package,FName(*FPackageName::GetLongPackageAssetName(Path)),
       BPTYPE_Normal,UBlueprint::StaticClass(),UBlueprintGeneratedClass::StaticClass());
   FAssetRegistryModule::AssetCreated(BP);
  }
  if (!BP || BP->UbergraphPages.IsEmpty()) return nullptr;
  UEdGraph* Graph=BP->UbergraphPages[0];
  const FName EventName(*Spec->GetStringField(TEXT("event")));
  const TArray<UEdGraphNode*> OldNodes=Graph->Nodes;
  for (UEdGraphNode* Node:OldNodes)
   if (auto* Event=Cast<UK2Node_Event>(Node); Event && Event->EventReference.GetMemberName()==EventName)
    FBlueprintEditorUtils::RemoveNode(BP,Node,true);
  UFunction* EventFunction=Parent->FindFunctionByName(EventName);
  UFunction* CallFunction=Parent->FindFunctionByName(FName(*Spec->GetStringField(TEXT("call"))));
  if (!EventFunction || !CallFunction) return nullptr;
  auto* Event=NewObject<UK2Node_Event>(Graph);
  Event->EventReference.SetExternalMember(EventName,EventFunction->GetOwnerClass());
  Event->bOverrideFunction=true;
  Graph->AddNode(Event,true,false);
  Event->CreateNewGuid(); Event->PostPlacedNewNode(); Event->AllocateDefaultPins();
  auto* Call=NewObject<UK2Node_CallFunction>(Graph);
  Call->SetFromFunction(CallFunction); Call->NodePosX=340;
  Graph->AddNode(Call,true,false);
  Call->CreateNewGuid(); Call->PostPlacedNewNode(); Call->AllocateDefaultPins();
  const UEdGraphSchema* Schema=Graph->GetSchema();
  if (!Schema->TryCreateConnection(Event->FindPin(UEdGraphSchema_K2::PN_Then),Call->GetExecPin())) return nullptr;
  FString Parameter;
  if (Spec->TryGetStringField(TEXT("parameter"),Parameter))
   if (!Schema->TryCreateConnection(Event->FindPin(FName(*Parameter)),Call->FindPin(FName(*Parameter)))) return nullptr;
  FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
  FKismetEditorUtilities::CompileBlueprint(BP);
  if (!BP->GeneratedClass || BP->Status==BS_Error || !ApplyDefaults(BP->GeneratedClass->GetDefaultObject(),Spec)) return nullptr;
  if (BP->Status==BS_Error || !SaveAsset(BP,Path)) return nullptr;
  return BP;
 }
}

UPortfolioForgeCommandlet::UPortfolioForgeCommandlet()
{
 IsClient=false; IsEditor=true; IsServer=false; LogToConsole=true;
}

int32 UPortfolioForgeCommandlet::Main(const FString& Params)
{
 FString Text;
 TSharedPtr<FJsonObject> Spec;
 if (!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectDir()/TEXT("Tools/forge.json"))) ||
     !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Spec)) return 1;
 TArray<TSharedPtr<FJsonValue>> Assets;
 const FString MaterialPath=TEXT("/Game/Portfolio/M_Surface");
 UPackage* MaterialPackage=CreatePackage(*MaterialPath);
 UMaterial* Material=NewObject<UMaterial>(MaterialPackage,TEXT("M_Surface"),RF_Public|RF_Standalone);
 auto* Tint=Cast<UMaterialExpressionVectorParameter>(
    UMaterialEditingLibrary::CreateMaterialExpression(Material,UMaterialExpressionVectorParameter::StaticClass()));
 if (!Tint) return 2;
 Tint->ParameterName=TEXT("Tint"); Tint->DefaultValue=FLinearColor(0.14f,0.32f,0.38f);
 UMaterialEditingLibrary::ConnectMaterialProperty(Tint,TEXT(""),MP_BaseColor);
 Material->PostEditChange();
 FAssetRegistryModule::AssetCreated(Material);
 if (!SaveAsset(Material,MaterialPath)) return 2;
 Assets.Add(MakeShared<FJsonValueString>(MaterialPath));
 for (const TSharedPtr<FJsonValue>& Value:Spec->GetArrayField(TEXT("blueprints")))
 {
  if (!BuildGraph(Value->AsObject())) return 3;
  Assets.Add(MakeShared<FJsonValueString>(Value->AsObject()->GetStringField(TEXT("path"))));
 }
 const TArray<TSharedPtr<FJsonValue>>* DataAssets;
 if (Spec->TryGetArrayField(TEXT("dataAssets"),DataAssets))
  for (const TSharedPtr<FJsonValue>& Value:*DataAssets)
  {
   const auto AssetSpec=Value->AsObject();
   const FString Path=AssetSpec->GetStringField(TEXT("path"));
   UClass* Class=LoadClass<UObject>(nullptr,*AssetSpec->GetStringField(TEXT("class")));
   if (!Class) return 9;
   UObject* Asset=NewObject<UObject>(CreatePackage(*Path),Class,FName(*FPackageName::GetLongPackageAssetName(Path)),RF_Public|RF_Standalone);
   if (!ApplyDefaults(Asset,AssetSpec)) return 10;
   FAssetRegistryModule::AssetCreated(Asset);
   if (!SaveAsset(Asset,Path)) return 11;
   Assets.Add(MakeShared<FJsonValueString>(Path));
  }
 UWorld* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);
 if (!World) return 4;
 UClass* WorldClass=LoadClass<AActor>(nullptr,*Spec->GetStringField(TEXT("world")));
 UClass* ModeClass=LoadClass<AGameModeBase>(nullptr,*Spec->GetStringField(TEXT("mode")));
 if (!WorldClass || !ModeClass) return 5;
 World->GetWorldSettings()->DefaultGameMode=ModeClass;
 if (!World->SpawnActor<AActor>(WorldClass)) return 6;
 APlayerStart* Start=World->SpawnActor<APlayerStart>();
 Start->SetActorLocation(FVector(-500,0,180));
 const FString Map=Spec->GetStringField(TEXT("map"));
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(FPackageName::LongPackageNameToFilename(Map,FPackageName::GetMapPackageExtension())),true);
 if (!UEditorLoadingAndSavingUtils::SaveMap(World,Map)) return 7;
 Assets.Add(MakeShared<FJsonValueString>(Map));
 auto Receipt=MakeShared<FJsonObject>();
 Receipt->SetBoolField(TEXT("success"),true);
 Receipt->SetArrayField(TEXT("assets"),Assets);
 FString Serialized;
 FJsonSerializer::Serialize(Receipt,TJsonWriterFactory<>::Create(&Serialized));
 return FFileHelper::SaveStringToFile(Serialized,*(FPaths::ProjectSavedDir()/TEXT("PortfolioAssets.json")))?0:8;
}
