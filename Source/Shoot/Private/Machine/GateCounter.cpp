#include "Machine/GateCounter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "ShootCharacter.h"
#include "Game/BaseGameInstance.h"
#include "Game/BusinessSimulation.h"
#include "Game/BusinessTime.h"

AGateCounter::AGateCounter()
{
	PrimaryActorTick.bCanEverTick = false;

	GateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GateMesh"));
	RootComponent = GateMesh;

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(RootComponent);
	InteractionSphere->SetSphereRadius(220.f);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	InteractionSphere->SetGenerateOverlapEvents(true);

	// 交互提示控件（如“按 E 开门营业”）：默认隐藏，靠近时显示；Widget Class 由蓝图在组件上配置
	TipsWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("TipsWidget"));
	TipsWidget->SetupAttachment(RootComponent);
	TipsWidget->SetVisibility(false);
}

void AGateCounter::BeginPlay()
{
	Super::BeginPlay();

	if (InteractionSphere)
	{
		// 确保交互球始终可生成重叠事件（防止蓝图层覆盖碰撞导致“靠近没反应”）
		InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
		InteractionSphere->SetGenerateOverlapEvents(true);
		if (InteractionSphere->GetUnscaledSphereRadius() <= 0.f)
		{
			InteractionSphere->SetSphereRadius(220.f);
		}

		InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &AGateCounter::OnInteractionOverlap);
		InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &AGateCounter::OnInteractionEndOverlap);
	}
}

UTimeSystem* AGateCounter::GetTimeSystem() const
{
	if (UWorld* W = GetWorld())
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(W->GetGameInstance()))
		{
			if (GI->businessSimulation)
			{
				return GI->businessSimulation->TimeSystem;
			}
		}
	}
	return nullptr;
}

bool AGateCounter::OpenBusiness()
{
	UTimeSystem* TS = GetTimeSystem();
	const bool bOk = TS ? TS->OpenBusiness() : false;
	OnOpenResult(bOk);
	return bOk;
}

void AGateCounter::OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (AShootCharacter* Player = Cast<AShootCharacter>(OtherActor))
	{
		if (bDebug && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow,
				FString::Printf(TEXT("[Gate] 重叠注册成功：%s 进入交互范围"), *GetNameSafe(Player)));
		}

		Player->SetOverlappingGate(this);

		// 当提示控件未被其它玩家占用时，显示“按 E 开门营业”提示
		if (LocalPlayer == nullptr && TipsWidget)
		{
			TipsWidget->SetVisibility(true);
			if (APawn* AP = Cast<APawn>(OtherActor))
			{
				if (APlayerController* APC = Cast<APlayerController>(AP->GetController()))
				{
					if (ULocalPlayer* LP = APC->GetLocalPlayer())
					{
						// 设置 OwnerPlayer，使控件归属于该本地玩家（多人分屏隔离）
						TipsWidget->SetOwnerPlayer(LP);
						LocalPlayer = LP;
					}
				}
			}
		}
	}
}

void AGateCounter::OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AShootCharacter* Player = Cast<AShootCharacter>(OtherActor))
	{
		// 仅当仍指向自己时才清空，避免多台串扰
		if (Player->GetOverlappingGate() == this)
		{
			Player->SetOverlappingGate(nullptr);
		}

		// 离开时隐藏提示控件（仅当占用者正是该玩家）
		if (LocalPlayer && TipsWidget)
		{
			if (APawn* AP = Cast<APawn>(OtherActor))
			{
				if (APlayerController* APC = Cast<APlayerController>(AP->GetController()))
				{
					if (ULocalPlayer* LP = APC->GetLocalPlayer())
					{
						if (LocalPlayer == LP)
						{
							TipsWidget->SetVisibility(false);
							LocalPlayer = nullptr;
						}
					}
				}
			}
		}
	}
}
