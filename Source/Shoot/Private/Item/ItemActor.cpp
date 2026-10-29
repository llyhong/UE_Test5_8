// Fill out your copyright notice in the Description page of Project Settings.

#include "Item/ItemActor.h"
#include "Data/ItemActorInfo.h"
#include "Components/SphereComponent.h"
#include "Interfaces/PickupInterface.h"
#include "ShootCharacter.h"
#include "NPC/ChairSeat.h"
#include "Game/BaseFoodActor.h"

#include "Game/PlateSystem.h"
#include "Game/BaseGameInstance.h"
#include "Kismet/GameplayStatics.h"

AItemActor::AItemActor()
{
	PrimaryActorTick.bCanEverTick = true;

	ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMeshComponent"));
	ItemMesh->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
	ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = ItemMesh;

	HandIK_LPoint = CreateDefaultSubobject<USceneComponent>(TEXT("HandIK_LPoint"));
	HandIK_LPoint->SetupAttachment(GetRootComponent());
	HandIK_RPoint = CreateDefaultSubobject<USceneComponent>(TEXT("HandIK_RPoint"));
	HandIK_RPoint->SetupAttachment(GetRootComponent());
	
	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->SetupAttachment(GetRootComponent());

	// 空碟回收交互球：默认关闭，仅当本物品成为“桌上空碟”后才启用
	CollectSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollectSphere"));
	CollectSphere->SetupAttachment(GetRootComponent());
	CollectSphere->SetSphereRadius(140.f);
	CollectSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollectSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	CollectSphere->SetGenerateOverlapEvents(false);
}

void AItemActor::BeginPlay()
{
	Super::BeginPlay();
	
	//绑定回调函数
	Sphere->OnComponentBeginOverlap.AddDynamic(this, &AItemActor::OnSphereOverlap);
	Sphere->OnComponentEndOverlap.AddDynamic(this, &AItemActor::OnSphereEndOverlap);

	// 空碟回收球（仅在成为空碟后启用碰撞，届时才会触发）
	CollectSphere->OnComponentBeginOverlap.AddDynamic(this, &AItemActor::OnCollectOverlap);
	CollectSphere->OnComponentEndOverlap.AddDynamic(this, &AItemActor::OnCollectEndOverlap);

	// 食材解锁：若配置了 IngredientUnlockID(>=0)，则按其是否已解锁决定能否交互；
	// 并订阅 GameInstance 的解锁广播，运行时解锁后即时开放交互。
	if (IngredientUnlockID >= 0)
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
		{
			bIngredientUnlocked = GI->IsIngredientUnlocked(IngredientUnlockID);
			GI->OnIngredientsUnlocked.AddDynamic(this, &AItemActor::HandleIngredientsUnlocked);
		}
		else
		{
			// 没有 GameInstance 时保守视为锁定，避免未配置即被拾取
			bIngredientUnlocked = false;
		}
		OnIngredientUnlockStateChanged.Broadcast(bIngredientUnlocked);
	}
}

bool AItemActor::CanInteract()
{
	EItemState state = GetItemState();
	return state == EItemState::EIS_Equipped || state == EItemState::EIS_Cooking;
}

void AItemActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// if (OverlayMaterial && GetItemState() == EItemState::EIS_Hovering)
	// {
	// 	ItemMesh->SetOverlayMaterial(OverlayMaterial);
	// }
	// else
	// {
	// 	ItemMesh->SetOverlayMaterial(nullptr);
	// }
	bool needOutline = NeedOutLineEffect();
	if (bSetOutline != needOutline)
	{
		bSetOutline = needOutline;
		SetActorsOutline(this,bSetOutline);
	}

	// —— 成品腐烂计时 ——
	// 判定"是否在玩家手上"以 ItemState 为准：Equipped = 被持有（捡起），其余（Idle/Hovering/Cooking）视为未持有。
	// 玩家拾取起来 → 计时【暂停】（保留已累计时长，不清零）；放下后回到 Idle/Hovering → 从原值【继续累计】；
	// 累计超过 DecayTime 仍未被持有时 → 自动销毁（即成品腐烂）。
	// 临近腐烂（剩余 <= DecayWarnLeadTime）时广播一次 OnDecayWarning，供蓝图变色/动画；捡起（暂停）复位以便再次提示。
	if (bEnableDecay)
	{
		const bool bHeldByPlayer = (GetItemState() == EItemState::EIS_Equipped);
		if (!bHeldByPlayer)
		{
			DecayTimer += DeltaTime;

			// 进入临近警告窗口且尚未通知过 → 广播一次
			if (!bDecayWarned && DecayWarnLeadTime > 0.f && DecayTime > DecayWarnLeadTime
				&& DecayTimer >= (DecayTime - DecayWarnLeadTime))
			{
				bDecayWarned = true;
				OnDecayWarning.Broadcast();
				if (bShowDebugInfo && GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Orange,
						FString::Printf(TEXT("[ItemActor] %s 临近腐烂，通知蓝图"), *GetName()));
				}
			}

			if (DecayTimer >= DecayTime)
			{
				UE_LOG(LogTemp, Log, TEXT("[ItemActor] 成品 %s 未被拾取超过 %.1fs，腐烂销毁"), *GetName(), DecayTime);
				OnDecayed.Broadcast();
				Destroy();
				return;
			}
		}
		else
		{
			// 在手上（Equipped）：计时暂停，不清零。若之前已预警，则复位状态，放下后再次临近可重新提示。
			if (bDecayWarned)
			{
				bDecayWarned = false;
			}
		}
	}

	// —— 熟度计时（仅在树上结果中推进；采摘离树后由 StopRipening 关闭） ——
	if (bTrackRipening && bEnableRipeness)
	{
		RipenessTimer += DeltaTime;

		int32 NewStage = 0;
		if (RipenessTimer >= OverripeTime)
		{
			NewStage = 2;
		}
		else if (RipenessTimer >= RipeTime)
		{
			NewStage = 1;
		}

		if (NewStage != CurrentRipenessStage)
		{
			CurrentRipenessStage = NewStage;
			OnRipenessChanged.Broadcast(CurrentRipenessStage, GetRipenessStageName());
		}
	}
}

void AItemActor::ConfigureAndStartRipening(bool bEnable, float InRipeTime, float InOverripeTime)
{
	bEnableRipeness = bEnable;
	RipeTime = FMath::Max(0.f, InRipeTime);
	OverripeTime = FMath::Max(0.f, InOverripeTime);
	RipenessTimer = 0.f;
	CurrentRipenessStage = 0;
	bTrackRipening = true; // 开始累计熟度（在树上）
}

void AItemActor::StopRipening()
{
	bTrackRipening = false; // 离树：停止累计，品质已固定（采摘瞬间已写入 IngredientQuality）
}

FName AItemActor::GetRipenessStageName() const
{
	if (!bEnableRipeness)
	{
		return FName(TEXT("结果"));
	}
	if (RipenessTimer >= OverripeTime)
	{
		return FName(TEXT("熟过头"));
	}
	if (RipenessTimer >= RipeTime)
	{
		return FName(TEXT("成熟"));
	}
	return FName(TEXT("未成熟"));
}

float AItemActor::GetRipenessProgress() const
{
	if (!bEnableRipeness || OverripeTime <= 0.f)
	{
		return 0.f;
	}
	return FMath::Clamp(RipenessTimer / OverripeTime, 0.f, 1.f);
}

EIngredientQuality AItemActor::GetCurrentQuality() const
{
	if (!bEnableRipeness)
	{
		return EIngredientQuality::Normal;
	}
	if (RipenessTimer >= OverripeTime)
	{
		return EIngredientQuality::Inferior; // 熟过头 -> 次品
	}
	if (RipenessTimer >= RipeTime)
	{
		return EIngredientQuality::Premium; // 成熟 -> 佳品
	}
	return EIngredientQuality::Normal; // 尚未成熟 -> 普通
}

void AItemActor::EnableDecay(float InDecayTime)
{
	bEnableDecay = true;
	DecayTime = FMath::Max(0.1f, InDecayTime);
	DecayTimer = 0.f;
}
int32 AItemActor::GetItemID() const
{
	if (const FItemActorInfo* Row = ItemInfoRow.GetRow<FItemActorInfo>(TEXT("ItemActor::GetItemID")))
	{
		// 以数据表行名为准（行名为纯数字即 ItemID），行名不是数字时才退回行内 ItemID 字段
		return Row->ResolveItemID(ItemInfoRow.RowName);
	}
	return -1;
}

UTexture2D* AItemActor::GetItemIcon() const
{
	if (const FItemActorInfo* Row = ItemInfoRow.GetRow<FItemActorInfo>(TEXT("ItemActor::GetItemIcon")))
	{
		return Row->Icon;
	}
	return nullptr;
}

void AItemActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 安全兜底：若空碟在未被玩家收走的情况下被销毁（如关卡卸载），
	// 释放其占据的座位，避免座位残留“幽灵占用”导致新客人无法入座。
	if (OwningSeat && OwningSeat->DirtyPlate.Get() == this)
	{
		OwningSeat->ClearDirtyPlate();
		OwningSeat = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AItemActor::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 禁止拾取：作为场景普通物件（如由植物统一采摘的树上果实），不参与任何拾取交互
	if (!bPickable)
	{
		return;
	}
	// 食材未解锁：屏蔽一切拾取交互（不进入 Hovering、不注册为可拾取物）
	if (IngredientUnlockID >= 0 && !bIngredientUnlocked)
	{
		return;
	}
	if (CanInteract())
	{
		return;
	}
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("OnSphereOverlap"));
	IPickupInterface* PickupInterface = Cast<IPickupInterface>(OtherActor);
	if (PickupInterface)
	{
		// 解锁瞬间重判时 OtherComp 可能为 null；为空时视为非 UI 预设，仍允许注册为可拾取物
		const bool bIsUIPreset = OtherComp && OtherComp->GetCollisionProfileName() == "UI";
		if (bIsUIPreset)
		{
			// GEngine->AddOnScreenDebugMessage(
		 //   -1,                    // Key
		 //   5.f,                   // 显示时间
		 //   FColor::Yellow,        // 颜色
		 //   FString::Printf(TEXT("Primitive Component Name: %s"), *Name) );
			return;
		}
		PickupInterface->SetOverlappingItem(this);
		SetItemState(EItemState::EIS_Hovering);
		OnSphereOverlapEvent();
	}

}

void AItemActor::OnSphereEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	// 禁止拾取：本身不参与拾取交互，无需处理重叠结束逻辑
	if (!bPickable)
	{
		return;
	}
	// 食材未解锁：无需处理拾取重叠结束逻辑
	if (IngredientUnlockID >= 0 && !bIngredientUnlocked)
	{
		return;
	}
	if (CanInteract())
	{
		return;
	}
	if (GetItemState() != EItemState::EIS_Hovering)
	{
		return;
	}
	IPickupInterface* PickupInterface = Cast<IPickupInterface>(OtherActor);
	if (PickupInterface)
	{
		PickupInterface->SetOverlappingItem(nullptr);
		SetItemState(EItemState::EIS_Idle);
		OnSphereEndOverlapEvent();	
	}
}

void AItemActor::SetIngredientUnlocked(bool bUnlocked)
{
	if (bIngredientUnlocked == bUnlocked)
	{
		return;
	}
	bIngredientUnlocked = bUnlocked;
	OnIngredientUnlockStateChanged.Broadcast(bUnlocked);

	// 解锁瞬间若玩家正站在本物品交互球内，立即重发一次“进入重叠”逻辑，
	// 否则（重叠事件不会因解锁状态变化而重发）需走出再走进才能看到高亮/拾取。
	if (bUnlocked && Sphere)
	{
		TArray<AActor*> OverlappingActors;
		Sphere->GetOverlappingActors(OverlappingActors);
		for (AActor* A : OverlappingActors)
		{
			if (IsValid(A))
			{
				OnSphereOverlap(Sphere, A, nullptr, 0, false, FHitResult());
			}
		}
	}
}

void AItemActor::HandleIngredientsUnlocked(const TArray<int32>& UnlockedIDs)
{
	if (IngredientUnlockID >= 0 && UnlockedIDs.Contains(IngredientUnlockID))
	{
		SetIngredientUnlocked(true);
	}
}

void AItemActor::SetActorsOutline(AActor* actor, bool bEnable)
{
	TArray<UStaticMeshComponent*> StaticMeshComps;
	actor->GetComponents<UStaticMeshComponent>(StaticMeshComps);
	for (UStaticMeshComponent* MeshComp : StaticMeshComps)
	{
		if (MeshComp && MeshComp->IsValidLowLevel()) // 安全检查，确保指针有效
		{
			// 3. 启用该组件的自定义深度渲染
			MeshComp->SetRenderCustomDepth(bEnable);
			// 4. （可选）设置自定义模板值，例如设置为1用于后续的描边效果
			//MeshComp->SetCustomDepthStencilValue(1);
		}
	}
}

void AItemActor::OnClear()
{
	FoodIdArray.Empty();
	for (int32 i = 0; i < FoodArray.Num(); i++)
	{
		FoodArray[i]->Destroy();
	}
	FoodArray.Empty();
}

bool AItemActor::NeedOutLineEffect()
{
	return  GetItemState() == EItemState::EIS_Hovering;
}

void AItemActor::AttachMeshToSocket(USceneComponent* InParent, const FName& InSocketName)
{
	FAttachmentTransformRules TransformRules(EAttachmentRule::SnapToTarget, true);
	ItemMesh->AttachToComponent(InParent, TransformRules, InSocketName);
}

void AItemActor::DeAttachMeshToSocket()
{
	//FAttachmentTransformRules TransformRules(EAttachmentRule::SnapToTarget, true);
	ItemMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
}

void AItemActor::OnCollectOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 仅“桌上空碟”可被收走；普通物品（食材/端在手上/在桌上等待进食）不响应
	if (!bIsEmptyPlate)
	{
		return;
	}
	if (AShootCharacter* Player = Cast<AShootCharacter>(OtherActor))
	{
		Player->SetOverlappingEmptyPlate(this);
	}
}

void AItemActor::OnCollectEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AShootCharacter* Player = Cast<AShootCharacter>(OtherActor))
	{
		if (Player->GetOverlappingEmptyPlate() == this)
		{
			Player->SetOverlappingEmptyPlate(nullptr);
		}
	}
}

bool AItemActor::PickUpByPlayer(AShootCharacter* Player)
{
	if (!Player || !bIsEmptyPlate)
	{
		return false;
	}
	// 手上已有东西则无法再收走空碟（一只手只能拿一个）
	if (Player->GetPickingItem() != nullptr)
	{
		return false;
	}

	// 碟子离开桌面 → 座位立即释放（下一位客人可以入座）
	if (OwningSeat)
	{
		OwningSeat->ClearDirtyPlate();
		OwningSeat = nullptr;
	}

	// 不再是“桌上空碟”，改为玩家手上的“脏碟”，需交给洗碗机清洗
	bIsEmptyPlate = false;
	bIsDirtyPlate = true;
	bPickable = false;       // 脏碟不能被当作普通可捡物放下/丢弃，只能交还洗碗机
	bEnableDecay = false;
	if (CollectSphere)
	{
		CollectSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CollectSphere->SetGenerateOverlapEvents(false);
	}

	// 拿在手上（挂到手部插槽并设为主手持物）
	Player->HoldItemInHand(this, true);
	return true;
}