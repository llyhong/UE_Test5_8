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
#include "Interaction/InteractionConfig.h"

AGateCounter::AGateCounter()
{
	PrimaryActorTick.bCanEverTick = false;

	GateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GateMesh"));
	RootComponent = GateMesh;

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(RootComponent);
	InteractionSphere->SetSphereRadius(InteractionRadius * InteractionScale::Get());
	// 视口里未选中也画出判定球，摆放关卡时能直接目测交互范围
	InteractionSphere->ShapeColor = FColor(180, 100, 230);
	InteractionSphere->bDrawOnlyIfSelected = false;
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	InteractionSphere->SetGenerateOverlapEvents(true);

	// 交互提示控件（如“按 E 开门营业”）：默认隐藏，靠近时显示；Widget Class 由蓝图在组件上配置
	TipsWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("TipsWidget"));
	TipsWidget->SetupAttachment(RootComponent);
	TipsWidget->SetVisibility(false);
}

void AGateCounter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 编辑器视口预览：改 InteractionRadius 立刻反映到组件上，
	// 摆放关卡时看到的球就是运行时真正生效的判定范围
	ApplyInteractionScale();
}

void AGateCounter::ApplyInteractionScale()
{
	if (InteractionSphere)
	{
		InteractionSphere->SetSphereRadius(InteractionRadius * InteractionScale::Get(), false);
		InteractionSphere->SetRelativeLocation(FVector(0.f, 0.f, InteractionCenterHeight));
		InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
		InteractionSphere->SetGenerateOverlapEvents(true);
	}
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
		// 统一交互尺度：无条件按标准重设，覆盖蓝图里可能残留的旧半径
		ApplyInteractionScale();

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

		Player->AddOverlappingGate(this);

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
		// 只注销自己，避免多台大门范围交叠时互相清目标
		Player->RemoveOverlappingGate(this);

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
