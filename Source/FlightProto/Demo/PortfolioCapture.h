#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PortfolioCapture.generated.h"
UCLASS()
class UFlightCaptureSubsystem : public UTickableWorldSubsystem
{
 GENERATED_BODY()
public:
 bool IsReady() const {return bConfigured && Warmup>30 && !bFinished;}
 virtual void Tick(float Delta) override;
 virtual TStatId GetStatId() const override {RETURN_QUICK_DECLARE_CYCLE_STAT(PortfolioCapture,STATGROUP_Tickables);}
 virtual bool IsTickable() const override {return !IsTemplate();}
 virtual void Deinitialize() override;
private:
 FDelegateHandle Handle;
 FString Directory;
 int32 Frame=0,Limit=0,Warmup=0;
 bool bQueued=false,bConfigured=false,bFinished=false;
 void Captured(int32 Width,int32 Height,const TArray<FColor>& Colors);
 void FinishCapture(int32 Width,int32 Height);
};
