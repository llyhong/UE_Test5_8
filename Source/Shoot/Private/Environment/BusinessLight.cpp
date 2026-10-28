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

	// 默认熄灭：关卡加载瞬间一定为暗，避免“一进游戏营业就亮灯”。
	// 之后由 OnBusinessOpenChanged 事件（玩家到大门按 E 开门，或 bAutoOpenBusiness 自动开门）点亮。
	SetLit(false);

	BindTimeSystem(); // 仅绑定营业状态委托，不在此按当前状态强制亮灯
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
