#include "Demo/FlightDemoWorld.h"
#include "Demo/PortfolioVisual.h"

AFlightDemoWorld::AFlightDemoWorld()
{
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AFlightDemoWorld::OnConstruction(const FTransform& Transform)
{
 Super::OnConstruction(Transform);
 using namespace PortfolioVisual;
 Clear(this);
 Environment(this);
 const FLinearColor Grass(.13f,.24f,.23f), Dark(.05f,.075f,.095f), Light(.7f,.78f,.77f), Orange(.96f,.32f,.075f);
 Part(this,FVector(25000,0,-105),FVector(180000,180000,200),Grass);
 Part(this,FVector(20000,0,0),FVector(48000,2600,10),Dark);
 for (int32 i=0;i<40;++i)
 {
  Part(this,FVector(i*1100,0,8),FVector(480,35,4),Light,false);
  for (int32 Side:{-1,1})
   Part(this,FVector(i*1100,Side*1370,40),FVector(25,25,80),Orange,false);
 }
 for (int32 Side:{-1,1}) Part(this,FVector(20000,Side*1200,9),FVector(48000,15,4),Light,false);
 Part(this,FVector(-1000,4300,600),FVector(2200,2300,1200),Dark);
 Part(this,FVector(-1000,3100,900),FVector(1900,30,560),Orange);
 Part(this,FVector(-1000,3080,390),FVector(1800,20,180),Light);
 Label(this,TEXT("AERONAUT / ALPINE FIELD"),FVector(-2120,4200,1050),85);
 Part(this,FVector(2000,4400,1600),FVector(650,650,3200),Light);
 Part(this,FVector(2000,4400,3370),FVector(1250,1100,340),Dark);
 Part(this,FVector(2000,3830,3400),FVector(1000,20,220),FLinearColor(.12f,.42f,.5f),false);
 for (int32 i=0;i<14;++i)
  Part(this,FVector(i*10000-40000,-30000-(i%3)*6000,1700+(i%4)*700),
       FVector(18000,23000,9000+(i%4)*2500),FLinearColor(.19f,.28f,.3f),true,TEXT("Cone"));
 Label(this,TEXT("27"),FVector(-1600,0,60),170);
}

void AFlightDemoWorld::BeginPlay()
{
 Super::BeginPlay();OnConstruction(GetActorTransform());
}
