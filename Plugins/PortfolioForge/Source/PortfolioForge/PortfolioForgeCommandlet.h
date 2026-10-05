#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PortfolioForgeCommandlet.generated.h"

UCLASS()
class UPortfolioForgeCommandlet : public UCommandlet
{
 GENERATED_BODY()
public:
 UPortfolioForgeCommandlet();
 virtual int32 Main(const FString& Params) override;
};
