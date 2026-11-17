#include "Environment/BusinessLight.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Game/BaseGameInstance.h"
#include "Game/BusinessSimulation.h"
#include "Game/BusinessTime.h"

ABusinessLight::ABusinessLight()
{
	PrimaryActorTick.bCanEverTick = false;

	BulbMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BulbMesh"));
	RootComponent = BulbMesh;

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(RootComponent);
	Light->SetVisibility(false);
	Light->SetIntensity(0.f);
	Light->SetLightColor(LitColor);
	Light->SetAttenuationRadius(LitRadius);
}

void ABusinessLight::BeginPlay()
{
	Super::BeginPlay();

	// 先按"熄灭"初始化，绑定委托后再同步一次当前营业状态：
	// huanglily：关卡加载时若已处于营业中（自动开门早于灯 BeginPlay），补一次点亮，避免整店白天黑灯
	SetLit(false);
	if (UTimeSystem* TS = BindTimeSystem())
	{
		SetLit(TS->bIsBusinessOpen);
	}
}

UTimeSystem* ABusinessLight::BindTimeSystem()
{
	if (UWorld* W = GetWorld())
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(W->GetGameInstance()))
		{
			if (GI->businessSimulation && GI->businessSimulation->TimeSystem)
			{
				UTimeSystem* TS = GI->businessSimulation->TimeSystem;
				TS->OnBusinessOpenChanged.AddDynamic(this, &ABusinessLight::HandleBusinessOpenChanged);
				return TS;
			}
		}
	}
	return nullptr;
}

void ABusinessLight::SetLit(bool bLit)
{
	if (Light)
	{
		Light->SetVisibility(bLit);
		Light->SetIntensity(bLit ? LitIntensity : 0.f);
		Light->SetLightColor(LitColor);
		Light->SetAttenuationRadius(LitRadius);
	}
}

void ABusinessLight::HandleBusinessOpenChanged(bool bOpen)
{
	SetLit(bOpen);
}
