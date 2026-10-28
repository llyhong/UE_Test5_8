// Fill out your copyright notice in the Description page of Project Settings.

#include "Machine/Dishwasher.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Item/ItemActor.h"
#include "ShootCharacter.h"
#include "Game/PlateSystem.h"
#include "Game/BaseGameInstance.h"
#include "Game/BaseLevelBP.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

ADishwasher::ADishwasher()
{
	PrimaryActorTick.bCanEverTick = false;

	// 外观模型作为根组件，交互球挂在其上
	DishwasherMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DishwasherMesh"));
	RootComponent = DishwasherMesh;

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(RootComponent);
	InteractionSphere->SetSphereRadius(160.f);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	InteractionSphere->SetGenerateOverlapEvents(true);

	// 交互提示控件（如"按 E 洗碟子"）：默认隐藏，靠近时显示；Widget Class 由蓝图在组件上配置
	TipsWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("TipsWidget"));
	TipsWidget->SetupAttachment(RootComponent);
	TipsWidget->SetVisibility(false);
}

void ADishwasher::BeginPlay()
{
	Super::BeginPlay();

	InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &ADishwasher::OnInteractionOverlap);
	InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &ADishwasher::OnInteractionEndOverlap);

	// 在本洗碗机上直接配置本关碟子总数（>0 时覆盖盘子系统已有配置）
	if (TotalPlates > 0)
	{
		if (APlateSystem* PS = FindPlateSystem())
		{
			PS->SetTotalCount(TotalPlates);
			UE_LOG(LogTemp, Log, TEXT("[Dishwasher] 配置本关碟子总数 = %d（当前可用 %d）"), TotalPlates, PS->CurOrderIndex);
		}
	}
}

APlateSystem* ADishwasher::FindPlateSystem() const
{
	// 优先关卡蓝图上的实例
	if (const UWorld* W = GetWorld())
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(W->GetGameInstance()))
		{
			if (GI->curLevelBP && GI->curLevelBP->PlateSystem)
			{
				return GI->curLevelBP->PlateSystem;
			}
		}
	}

	// 回退：场景中第一个盘子系统
	if (UWorld* W = GetWorld())
	{
		return Cast<APlateSystem>(UGameplayStatics::GetActorOfClass(W, APlateSystem::StaticClass()));
	}
	return nullptr;
}

bool ADishwasher::SubmitPlate(AItemActor* Plate)
{
	if (!Plate)
	{
		OnPlateReturnFailed(TEXT("NoPlate"));
		return false;
	}

	// 只接受脏碟（玩家从桌上收走的），避免把做好的菜/食材误交还
	if (bRequireDirtyPlate && !Plate->bIsDirtyPlate)
	{
		UE_LOG(LogTemp, Log, TEXT("[Dishwasher] 交还失败：手持物(%s)不是待清洗的脏碟"), *Plate->GetName());
		OnPlateReturnFailed(TEXT("NotDirty"));
		return false;
	}

	APlateSystem* PS = FindPlateSystem();
	if (!PS)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Dishwasher] 交还失败：找不到盘子系统(APlateSystem)"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("[洗碗机] 找不到盘子系统(PlateSystem)"));
		}
		OnPlateReturnFailed(TEXT("NoPlateSystem"));
		return false;
	}

	// 可用碟子数 +1（上限为碟子总数 TotalCount）
	PS->AddPlate();
	const int32 Available = PS->CurOrderIndex;

	// 碟子消失
	Plate->Destroy();

	UE_LOG(LogTemp, Log, TEXT("[Dishwasher] 碟子已交还清洗：可用碟子 %d/%d"), Available, PS->TotalCount);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
			FString::Printf(TEXT("[洗碗机] 碟子已清洗，可用碟子 %d/%d"), Available, PS->TotalCount));
	}

	OnPlateReturned(Available);
	return true;
}

int32 ADishwasher::GetAvailablePlates() const
{
	if (const APlateSystem* PS = FindPlateSystem())
	{
		return PS->CurOrderIndex;
	}
	return 0;
}

int32 ADishwasher::GetTotalPlates() const
{
	if (const APlateSystem* PS = FindPlateSystem())
	{
		return PS->TotalCount;
	}
	return 0;
}

void ADishwasher::OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AShootCharacter* Character = Cast<AShootCharacter>(OtherActor);
	if (!Character)
	{
		return;
	}

	Character->SetOverlappingDishwasher(this);

	// 当提示控件未被其它玩家占用时，显示"按 E 洗碟子"提示
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

void ADishwasher::OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	AShootCharacter* Character = Cast<AShootCharacter>(OtherActor);
	if (!Character)
	{
		return;
	}

	if (Character->GetOverlappingDishwasher() == this)
	{
		Character->SetOverlappingDishwasher(nullptr);
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
