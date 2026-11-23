// Fill out your copyright notice in the Description page of Project Settings.

#include "Item/ItemActor.h"
#include "Item/PlateCollectionTray.h"
#include "Data/ItemActorInfo.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Interfaces/PickupInterface.h"
#include "ShootCharacter.h"
#include "NPC/ChairSeat.h"
#include "Game/BaseFoodActor.h"

#include "Game/PlateSystem.h"
#include "Game/BaseGameInstance.h"
#include "Game/BaseLevelBP.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DrawDebugHelpers.h"
#include "UI/PremiumDishFeedbackWidget.h"
#include "UI/DirtyPlateMarkerWidget.h"

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
	// 视口里未选中也画出判定球，摆放关卡时能直接目测拾取范围
	Sphere->ShapeColor = FColor(60, 200, 90);
	Sphere->bDrawOnlyIfSelected = false;

	// 空碟回收交互球：默认关闭，仅当本物品成为“桌上空碟”后才启用
	CollectSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollectSphere"));
	CollectSphere->SetupAttachment(GetRootComponent());
	CollectSphere->SetSphereRadius(InteractionDistance::Item);
	CollectSphere->ShapeColor = FColor(220, 150, 40);
	// 与 Sphere 同半径同球心，完全重合，默认不绘制（只在选中它时才画），避免视口里两球互相遮挡
	CollectSphere->bDrawOnlyIfSelected = true;
	CollectSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollectSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	CollectSphere->SetGenerateOverlapEvents(false);
}

void AItemActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 编辑器视口预览：改 Radius / CenterHeight 立刻反映到组件上，
	// 这样摆放关卡时看到的球就是运行时真正生效的判定范围
	ApplyInteractionScale();
}

void AItemActor::ApplyInteractionScale()
{
	// ============ 统一交互判定尺度 ============
	// 组件上的半径/位置可能被蓝图保存过旧值（引擎默认只有 32），这里强制按统一标准重设。
	// 不这么做的话，每个食材蓝图各写各的半径，玩家就会觉得“有的要贴脸、有的离很远”。
	// 半径按“世界空间”口径反算：个别成品蓝图（BP_Shala_Fried）把 Sphere 缩到了 0.35，
	// 直接写 44 时世界半径只剩 15，玩家站在灶台前完全够不到，表现为成品“看得见拿不到”。
	// 除以组件世界缩放后，不论蓝图里怎么缩放，判定范围都等于 InteractionRadius。
	if (Sphere)
	{
		const FVector Scale = Sphere->GetComponentScale();
		const float MaxScale = FMath::Max3(FMath::Abs(Scale.X), FMath::Abs(Scale.Y), FMath::Abs(Scale.Z));
		// huanglily: 相对位置只乘父级缩放（不是球自己的缩放），高度要按父级反算
		const FVector ParentScale = Sphere->GetAttachParent() ? Sphere->GetAttachParent()->GetComponentScale() : FVector(1.f);
		const float HeightScale = FMath::Max(FMath::Abs(ParentScale.Z), KINDA_SMALL_NUMBER);
		Sphere->SetSphereRadius(InteractionRadius * InteractionScale::Get() / FMath::Max(MaxScale, KINDA_SMALL_NUMBER), false);
		Sphere->SetRelativeLocation(FVector(0.f, 0.f, InteractionCenterHeight / HeightScale));
		Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
		Sphere->SetGenerateOverlapEvents(true);
	}
	if (CollectSphere)
	{
		// 空碟回收球与拾取球用同一尺度：同一件东西不该出现两套距离
		const FVector Scale = CollectSphere->GetComponentScale();
		const float MaxScale = FMath::Max3(FMath::Abs(Scale.X), FMath::Abs(Scale.Y), FMath::Abs(Scale.Z));
		const FVector ParentScale = CollectSphere->GetAttachParent() ? CollectSphere->GetAttachParent()->GetComponentScale() : FVector(1.f);
		const float HeightScale = FMath::Max(FMath::Abs(ParentScale.Z), KINDA_SMALL_NUMBER);
		CollectSphere->SetSphereRadius(InteractionRadius * InteractionScale::Get() / FMath::Max(MaxScale, KINDA_SMALL_NUMBER), false);
		CollectSphere->SetRelativeLocation(FVector(0.f, 0.f, InteractionCenterHeight / HeightScale));
	}
}

void AItemActor::BeginPlay()
{
	Super::BeginPlay();

	SetActorsOutline(this, false);
	ApplyInteractionScale();

	// 盘子/食材蓝图（BP_Plate、BP_Beef、BP_fried_egg 等）在组件模板里勾了 Simulate Physics，
	// 而构造函数为了让食物不挡玩家把 ItemMesh 设成 NoCollision + 全通道 Ignore。
	// 两者叠加 = "有重力、无碰撞"：物品一生成就开始自由落体，直接穿过地板掉出世界，
	// 表现为"第一份菜正常、第二份连盘子带菜一起没了"（第二个盘子已经掉到地板下面去了）。
	EnsurePhysicsIsSafe();

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
	RefreshInteractionHighlightRequests();

	// —— 空碟暖色光晕脉动（1.6s 一个呼吸周期）——
	if (DirtyPlateGlowLight)
	{
		DirtyPlateGlowPhase += DeltaTime;
		const float Glow = 0.5f + 0.5f * FMath::Sin(DirtyPlateGlowPhase * 2.f * PI / 1.6f);
		DirtyPlateGlowLight->SetIntensity(8.f + 6.f * Glow);
	}

	// 根组件就是 ItemMesh：投掷/掉落开启物理后整个 Actor 会翻滚，
	// 挂在根上的判定球跟着转到地面以下，物品明明在脚边却按不出来。
	// 只在物理模拟时把球心锁回 Actor 正上方，保持竖直方向的判定高度。
	if (ItemMesh && ItemMesh->IsSimulatingPhysics())
	{
		// huanglily: 物理翻滚后判定球也可能被压到地面下，这里固定世界竖直偏移
		const FVector Center = GetActorLocation() + FVector(0.f, 0.f, InteractionCenterHeight);
		if (Sphere)
		{
			Sphere->SetWorldLocation(Center, false, nullptr, ETeleportType::TeleportPhysics);
		}
		if (CollectSphere)
		{
			CollectSphere->SetWorldLocation(Center, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	// ============ 交互范围调试可视化（控制台 Shoot.DebugInteract 1） ============
	// 绿=当前拾取目标，灰=可拾取但不在目标位置。手持/烹饪中的物品不画，免得跟着手乱闪。
	if (InteractionDebug::IsEnabled() && Sphere)
	{
		const EItemState St = GetItemState();
		if (St == EItemState::EIS_Idle || St == EItemState::EIS_Hovering)
		{
			const FColor C = (St == EItemState::EIS_Hovering)
				? InteractionDebug::ColorCurrentTarget()
				: InteractionDebug::ColorIdle();
			DrawDebugSphere(GetWorld(), Sphere->GetComponentLocation(), Sphere->GetScaledSphereRadius(),
				16, C, false, 0.f, 0, 1.5f);
		}
	}

	// if (OverlayMaterial && GetItemState() == EItemState::EIS_Hovering)
	// {
	// 	ItemMesh->SetOverlayMaterial(OverlayMaterial);
	// }
	// else
	// {
	// 	ItemMesh->SetOverlayMaterial(nullptr);
	// }
	// 描边不再由 ItemState(Hovering) 驱动：状态是"我进了某个球的范围"，一堆食材都满足，
	// 于是会同时亮一片。描边的唯一开关是 SetInteractionHighlight()，由角色选出的
	// "当前唯一交互目标"调用。

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

FString AItemActor::GetItemDisplayName() const
{
	if (const FItemActorInfo* Row = ItemInfoRow.GetRow<FItemActorInfo>(TEXT("ItemActor::GetItemDisplayName")))
	{
		const FString& Name = Row->DisplayName.IsEmpty() ? Row->ItemDes : Row->DisplayName;
		if (!Name.IsEmpty())
		{
			return Name;
		}
	}
	return GetActorNameOrLabel();
}

void AItemActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		ReturnServingPlate();
	}
	// 安全兜底：若空碟在未被玩家收走的情况下被销毁（如关卡卸载），
	// 释放其占据的座位，避免座位残留“幽灵占用”导致新客人无法入座。
	if (OwningSeat && OwningSeat->DirtyPlate.Get() == this)
	{
		OwningSeat->ClearDirtyPlate();
		OwningSeat = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

bool AItemActor::ReserveServingPlate(APlateSystem& PlateSystem)
{
	if (!PlateSystem.IsInfinitePlates() && !HasReservedServingPlate())
	{
		if (PlateSystem.GetAvailablePlates() <= 0) return false;
		PlateSystem.RemovePlate();
		ReservedPlateSystem = &PlateSystem;
	}
	return true;
}

bool AItemActor::TryPrepareServingPlate()
{
	if (HasReservedServingPlate() || UGameplayStatics::GetCurrentLevelName(this) != TEXT("Kitchen_New")) return true;
	const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	APlateSystem* Plates = GI && GI->curLevelBP ? GI->curLevelBP->PlateSystem.Get() : nullptr;
	return Plates && ReserveServingPlate(*Plates);
}

bool AItemActor::ReturnServingPlate()
{
	APlateSystem* PlateSystem = ReservedPlateSystem.Get();
	ReservedPlateSystem.Reset();
	if (!PlateSystem)
	{
		return false;
	}
	PlateSystem->AddPlate();
	return true;
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
		PickupInterface->AddOverlappingItem(this);
		// 注意：这里不再顺手把自己置为 Hovering。
		// 进入交互球只代表"我是候选之一"，谁被高亮/被拾取由角色选出的唯一目标决定，
		// 否则一堆挨在一起的食材会在各自的 Overlap 回调里同时点亮描边。
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
	// 已被拿在手上/正在锅里烹饪的物品不参与候选集维护（它们不在拾取候选里）
	if (GetItemState() == EItemState::EIS_Equipped || GetItemState() == EItemState::EIS_Cooking)
	{
		return;
	}
	IPickupInterface* PickupInterface = Cast<IPickupInterface>(OtherActor);
	if (PickupInterface)
	{
		// 注销自己（带身份语义）：角色会从剩余候选里重选最近目标，避免误清别人的目标
		PickupInterface->RemoveOverlappingItem(this);
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

void AItemActor::ShowPremiumDishFeedback()
{
	if (IngredientQuality != EIngredientQuality::Premium || MenuFoodID < 0
		|| bIsEmptyPlate || bIsDirtyPlate || PremiumDishFeedback || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	PremiumDishFeedback = NewObject<UWidgetComponent>(this, TEXT("PremiumDishFeedback"));
	PremiumDishFeedback->SetupAttachment(ItemMesh);
	PremiumDishFeedback->SetWidgetSpace(EWidgetSpace::Screen);
	PremiumDishFeedback->SetWidgetClass(UPremiumDishFeedbackWidget::StaticClass());
	PremiumDishFeedback->SetDrawSize(FVector2D(104.f, 64.f));
	PremiumDishFeedback->SetPivot(FVector2D(0.5f, 1.f));
	PremiumDishFeedback->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PremiumDishFeedback->SetWindowFocusable(false);
	FVector Anchor(0.f, 0.f, 10.f);
	if (const UStaticMesh* Mesh = ItemMesh->GetStaticMesh())
	{
		const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
		Anchor = MeshBounds.Origin;
		Anchor.Z += MeshBounds.BoxExtent.Z;
	}
	PremiumDishFeedback->SetRelativeLocation(Anchor);
	PremiumDishFeedback->RegisterComponent();
}

void AItemActor::ClearPremiumDishFeedback()
{
	if (PremiumDishFeedback)
	{
		PremiumDishFeedback->DestroyComponent();
		PremiumDishFeedback = nullptr;
	}
}

void AItemActor::ShowDirtyPlateFeedback()
{
	if (!bIsEmptyPlate || DirtyPlateMarker || !ItemMesh || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	FVector Anchor(0.f, 0.f, 14.f);
	if (const UStaticMesh* Mesh = ItemMesh->GetStaticMesh())
	{
		const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
		Anchor = MeshBounds.Origin;
		Anchor.Z += MeshBounds.BoxExtent.Z + 2.f;
	}

	// 纯图形标记：几只绕飞苍蝇（虚线轨迹）+ 臭味飘线，不含任何文字；屏幕空间固定像素大小，远处也能看到
	DirtyPlateMarker = NewObject<UWidgetComponent>(this, TEXT("DirtyPlateMarker"));
	DirtyPlateMarker->SetupAttachment(ItemMesh);
	DirtyPlateMarker->SetWidgetSpace(EWidgetSpace::Screen);
	DirtyPlateMarker->SetWidgetClass(UDirtyPlateMarkerWidget::StaticClass());
	DirtyPlateMarker->SetDrawSize(FVector2D(128.f, 128.f));
	DirtyPlateMarker->SetPivot(FVector2D(0.5f, 1.f));
	DirtyPlateMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DirtyPlateMarker->SetWindowFocusable(false);
	DirtyPlateMarker->SetRelativeLocation(Anchor);
	DirtyPlateMarker->RegisterComponent();

	// 碟子本体的暖色光晕：小范围点光源从碟口上方往下打；
	// 亮度刻意压低，只给碟沿一圈暖边，避免白盘子被照成一块发光的白饼盖住残渍
	DirtyPlateGlowLight = NewObject<UPointLightComponent>(this, TEXT("DirtyPlateGlowLight"));
	DirtyPlateGlowLight->SetupAttachment(ItemMesh);
	DirtyPlateGlowLight->SetRelativeLocation(FVector(0.f, 0.f, Anchor.Z + 6.f));
	DirtyPlateGlowLight->SetLightColor(FLinearColor(1.f, 0.62f, 0.22f));
	DirtyPlateGlowLight->SetAttenuationRadius(120.f);
	DirtyPlateGlowLight->SetIntensity(10.f);
	DirtyPlateGlowLight->SetCastShadows(false);
	DirtyPlateGlowLight->SetMobility(EComponentMobility::Movable);
	DirtyPlateGlowLight->RegisterComponent();
	DirtyPlateGlowPhase = 0.f;
}

void AItemActor::ShowDirtyPlateResidue()
{
	if (!ItemMesh || DirtyPlateResidues.Num() > 0 || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	UStaticMesh* BlobMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	const UStaticMesh* PlateMesh = ItemMesh->GetStaticMesh();
	if (!BlobMesh || !BaseMat || !PlateMesh)
	{
		return;
	}

	// 只靠包围盒估算碟面高度：盘子取约半个盘深，碗取碗底上方一点；残渣略微嵌入表面避免悬空
	const FBoxSphereBounds Bounds = PlateMesh->GetBounds();
	const bool bBowl = Bounds.BoxExtent.Z > Bounds.BoxExtent.X * 0.4f;
	const float SurfaceZ = bBowl
		? Bounds.Origin.Z - Bounds.BoxExtent.Z + Bounds.BoxExtent.Z * 0.18f
		: Bounds.Origin.Z + Bounds.BoxExtent.Z * 0.4f;
	// 盘面半径（网格本地单位）：位置按它铺开，斑块尺寸按它缩放，两者分开算，
	// 否则大斑点会把坐标挤到中间，看上去只剩一坨
	const float PlateRadius = FMath::Min(Bounds.BoxExtent.X, Bounds.BoxExtent.Y);
	const float Spread = PlateRadius * (bBowl ? 0.7f : 0.95f);
	const FVector Center(Bounds.Origin.X, Bounds.Origin.Y, 0.f);

	struct FResiduePiece
	{
		float X;         // 相对碟心的水平偏移（Spread 的比例）
		float Y;
		float Radius;    // 斑块半径（PlateRadius 的比例）
		float Elongation; // Y 方向拉伸（<1 压扁成椭圆抹痕，>1 拉长）
		float Yaw;
		float R, G, B;
		float Roughness;
	};

	// 大面积的暗色抹痕铺满盘心 + 沿半径方向的细长酱痕 + 零星菜渣：
	// 全部压得很薄、粗糙度高，远看是一盘被吃过的残渍而不是几颗食物
	static const FResiduePiece Pieces[] =
	{
		{ -0.34f, 0.26f, 0.40f, 0.75f, 20.f, 0.16f, 0.075f, 0.035f, 0.80f },
		{ 0.26f, -0.28f, 0.34f, 0.60f, -30.f, 0.20f, 0.09f, 0.04f, 0.78f },
		{ 0.40f, 0.34f, 0.16f, 2.60f, 40.f, 0.28f, 0.12f, 0.05f, 0.70f },
		{ -0.44f, -0.16f, 0.14f, 2.20f, -15.f, 0.24f, 0.10f, 0.045f, 0.72f },
		{ 0.00f, -0.50f, 0.12f, 2.80f, 85.f, 0.18f, 0.08f, 0.04f, 0.75f },
		{ -0.12f, 0.52f, 0.10f, 1.80f, 10.f, 0.34f, 0.20f, 0.09f, 0.65f },
		{ 0.36f, -0.04f, 0.06f, 1.40f, 0.f, 0.08f, 0.04f, 0.02f, 0.85f },
		{ -0.30f, 0.10f, 0.06f, 1.20f, 0.f, 0.14f, 0.13f, 0.05f, 0.80f },
		{ 0.14f, 0.20f, 0.05f, 1.60f, 0.f, 0.42f, 0.24f, 0.10f, 0.58f },
		{ -0.20f, -0.30f, 0.05f, 1.30f, 0.f, 0.10f, 0.05f, 0.03f, 0.88f },
		{ 0.08f, -0.12f, 0.07f, 2.00f, 70.f, 0.30f, 0.14f, 0.06f, 0.70f },
		{ -0.50f, 0.44f, 0.08f, 1.40f, 55.f, 0.12f, 0.06f, 0.03f, 0.82f },
		{ 0.48f, -0.44f, 0.07f, 1.20f, -50.f, 0.20f, 0.09f, 0.04f, 0.80f },
		{ 0.00f, 0.02f, 0.09f, 1.50f, 5.f, 0.10f, 0.05f, 0.03f, 0.85f },
	};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Pieces); ++Index)
	{
		const FResiduePiece& Piece = Pieces[Index];
		const float Radius = PlateRadius * Piece.Radius;
		// 统一压成极薄的贴面：厚度只取半径的 3%，看上去是干在碟面上的抹痕
		const float HalfHeight = Radius * 0.03f;

		UStaticMeshComponent* Blob = NewObject<UStaticMeshComponent>(this,
			*FString::Printf(TEXT("DirtyPlateResidue%d"), Index));
		Blob->SetupAttachment(ItemMesh);
		Blob->SetStaticMesh(BlobMesh);
		Blob->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Blob->SetGenerateOverlapEvents(false);
		Blob->SetCastShadow(false);
		Blob->SetMobility(EComponentMobility::Movable);
		Blob->SetRelativeLocation(FVector(
			Center.X + Piece.X * Spread,
			Center.Y + Piece.Y * Spread,
			SurfaceZ + HalfHeight * 0.1f));
		// 球体基网格半径 50：压成扁椭圆，代表酱渍 / 菜渣
		Blob->SetRelativeScale3D(FVector(
			Radius / 50.f, Radius * Piece.Elongation / 50.f, HalfHeight / 50.f));
		Blob->SetRelativeRotation(FRotator(0.f, Piece.Yaw, 0.f));

		UMaterialInstanceDynamic* BlobMat = UMaterialInstanceDynamic::Create(BaseMat, this);
		BlobMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(Piece.R, Piece.G, Piece.B));
		BlobMat->SetScalarParameterValue(TEXT("Roughness"), Piece.Roughness);
		Blob->SetMaterial(0, BlobMat);

		Blob->RegisterComponent();
		DirtyPlateResidues.Add(Blob);
	}
}

void AItemActor::ClearDirtyPlateFeedback()
{
	if (DirtyPlateMarker)
	{
		DirtyPlateMarker->DestroyComponent();
		DirtyPlateMarker = nullptr;
	}
	if (DirtyPlateGlowLight)
	{
		DirtyPlateGlowLight->DestroyComponent();
		DirtyPlateGlowLight = nullptr;
	}
}

void AItemActor::OnClear()
{
	ClearPremiumDishFeedback();
	FoodIdArray.Empty();
	for (int32 i = 0; i < FoodArray.Num(); i++)
	{
		FoodArray[i]->Destroy();
	}
	FoodArray.Empty();
}

bool AItemActor::NeedOutLineEffect()
{
	// 描边只认角色下发的"唯一交互目标"标记，不再看 ItemState
	return bInteractionHighlight;
}

void AItemActor::SetInteractionHighlight(bool bEnable)
{
	bManualHighlightRequested = bEnable;
	RefreshInteractionHighlightRequests();
}

void AItemActor::SetInteractionHighlightForPlayer(AActor* Player, bool bEnable)
{
	if (Player)
	{
		const TWeakObjectPtr<AActor> Requester(Player);
		if (bEnable && IsValid(Player))
		{
			InteractionHighlightRequesters.Add(Requester);
		}
		else
		{
			InteractionHighlightRequesters.Remove(Requester);
		}
	}
	RefreshInteractionHighlightRequests();
}

void AItemActor::RefreshInteractionHighlightRequests()
{
	for (auto It = InteractionHighlightRequesters.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}
	const bool bEnable = bManualHighlightRequested || !InteractionHighlightRequesters.IsEmpty();
	bInteractionHighlight = bEnable;
	if (bSetOutline != bEnable)
	{
		bSetOutline = bEnable;
		SetActorsOutline(this, bEnable);
	}
}

void AItemActor::AttachMeshToSocket(USceneComponent* InParent, const FName& InSocketName)
{
	// 拿在手上/摆到灶台上时必须是"纯视觉"状态：关掉物理与碰撞。
	// 否则被投掷过一次的物品（物理已开启）再被拾取，物理仍在跑，会挣脱挂点掉穿地板。
	ItemMesh->SetSimulatePhysics(false);
	ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// huanglily：SnapToTarget 会连插槽缩放一起继承；角色 RetargetMesh 的手部插槽带 2.25 骨骼缩放，
	// 食物一拿起就变大。先记下当前世界缩放，挂接后按插槽世界缩放反算，保持物品原本大小。
	bAttachScaleCompensated = false;
	if (bPreserveWorldScaleOnAttach && InParent)
	{
		AttachWorldScaleBefore = ItemMesh->GetComponentScale();
		bAttachScaleCompensated = true;
	}

	FAttachmentTransformRules TransformRules(EAttachmentRule::SnapToTarget, true);
	ItemMesh->AttachToComponent(InParent, TransformRules, InSocketName);

	if (bAttachScaleCompensated)
	{
		const FVector SocketScale = (!InSocketName.IsNone() && InParent->DoesSocketExist(InSocketName))
			? InParent->GetSocketTransform(InSocketName, ERelativeTransformSpace::RTS_World).GetScale3D()
			: InParent->GetComponentTransform().GetScale3D();
		auto SafeScale = [](float S) { return FMath::Max(FMath::Abs(S), KINDA_SMALL_NUMBER); };
		ItemMesh->SetRelativeScale3D(FVector(
			AttachWorldScaleBefore.X / SafeScale(SocketScale.X),
			AttachWorldScaleBefore.Y / SafeScale(SocketScale.Y),
			AttachWorldScaleBefore.Z / SafeScale(SocketScale.Z)));
	}
}

void AItemActor::EnablePhysicsDrop()
{
	if (!ItemMesh)
	{
		return;
	}

	ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ItemMesh->SetCollisionObjectType(ECC_PhysicsBody);
	ItemMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	// 只和"世界几何"碰撞：地板、墙、桌子、其它掉落的食物
	ItemMesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	ItemMesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	ItemMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	// 明确不挡角色：食物落地后不该成为路障
	ItemMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	ItemMesh->SetUseCCD(true);
	ItemMesh->SetEnableGravity(true);

	if (!ItemMesh->IsSimulatingPhysics())
	{
		ItemMesh->SetSimulatePhysics(true);
	}
}

void AItemActor::EnsurePhysicsIsSafe()
{
	if (!ItemMesh || !ItemMesh->IsSimulatingPhysics())
	{
		// 没开物理的物品（绝大多数场景摆放/手持状态）保持纯视觉，不做任何额外碰撞开销。
		// 注意不能用 IsGravityEnabled() 判断：它默认就是 true，但不开模拟物理不会下落。
		return;
	}

	// 复用手掷/掉落那套设置：只 Block 世界几何、Ignore Pawn、开 CCD。
	EnablePhysicsDrop();
}

void AItemActor::DeAttachMeshToSocket()
{
	//FAttachmentTransformRules TransformRules(EAttachmentRule::SnapToTarget, true);
	ItemMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

	// 挂接时做过缩放补偿的物品，摘下后还原成挂接前的世界大小（否则会把插槽缩放一起带走）
	if (bAttachScaleCompensated)
	{
		ItemMesh->SetWorldScale3D(AttachWorldScaleBefore);
		bAttachScaleCompensated = false;
	}
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
		Player->AddOverlappingEmptyPlate(this);
	}
}

void AItemActor::OnCollectEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AShootCharacter* Player = Cast<AShootCharacter>(OtherActor))
	{
		// 只注销自己：相邻几张桌子的空碟范围交叠时，其它空碟仍是候选目标
		Player->RemoveOverlappingEmptyPlate(this);
	}
}

bool AItemActor::PickUpByPlayer(AShootCharacter* Player)
{
	if (!Player || !bIsEmptyPlate)
	{
		return false;
	}
	// 手上已有东西则无法再收走空碟（一只手只能拿一个）
	APlateCollectionTray* Tray = Cast<APlateCollectionTray>(Player->GetPickingItem());
	if (Player->GetPickingItem() && (!Tray || !Tray->HasRoom()))
	{
		return false;
	}

	// 碟子离开桌面 → 座位立即释放（下一位客人可以入座）
	AChairSeat* Seat = OwningSeat.Get();
	OwningSeat = nullptr;

	// 不再是“桌上空碟”，改为玩家手上的“脏碟”，需交给洗碗机清洗
	bIsEmptyPlate = false;
	bIsDirtyPlate = true;
	ClearDirtyPlateFeedback();
	bPickable = false;       // 脏碟不能被当作普通可捡物放下/丢弃，只能交还洗碗机
	bEnableDecay = false;
	if (CollectSphere)
	{
		CollectSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CollectSphere->SetGenerateOverlapEvents(false);
	}

	// 拿在手上（挂到手部插槽并设为主手持物）
	if (Tray)
	{
		Tray->AddPlate(this);
	}
	else
	{
		Player->HoldItemInHand(this, true);
	}
	if (IsValid(Seat))
	{
		Seat->ClearDirtyPlate();
	}
	return true;
}
