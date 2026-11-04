 // Copyright Epic Games, Inc. All Rights Reserved.

#include "ShootCharacter.h"
#include "Engine/LocalPlayer.h"
#include "InventoryWidget.h"
#include "UI/ShopWidget.h"
#include "Machine/CookingMachine.h"
#include "Machine/ServingCounter.h"
#include "Machine/AttractCounter.h"
#include "Machine/Dishwasher.h"
#include "Machine/ResearchStove.h"
#include "NPC/GuestCharacter.h"
#include "NPC/ChairSeat.h"
#include "Dialogue/DialogueComponent.h"
#include "Item/Plant.h"
#include "Item/ItemActor.h"
#include "Item/FoodSpawner.h"
#include "Item/Dish.h"
#include "Item/Desk.h"
#include "Item/FoodSpawnOther.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "UObject/ConstructorHelpers.h"
#include "Shoot.h"
#include "Animation/AnimMontage.h"
#include "Animation/ShootAnimInstance.h"
#include "Components/PostProcessComponent.h"
#include "Game/BaseGameInstance.h"
#include "Game/BaseLevelBP.h"
#include "Machine/StorageWarehouse.h"
#include "UI/HUDWidget.h"
#include "Machine/GateCounter.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/GameplayStaticsTypes.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Data/ItemActorInfo.h"
#include "Interaction/InteractionConfig.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"


void AShootCharacter::PlayPickMontage(FName SelectionName)
{
	// 手持物品（EAS_Picking）时也允许播放：DoPick 先置 EAS_Picking 再播动画，
	// 旧逻辑只放行 EAS_Unoccupied 会导致拾取动画永远不播
	if (CharacterActionState == ECharacterActionState::EAS_Unoccupied
		|| CharacterActionState == ECharacterActionState::EAS_Picking)
	{
		UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
		if (AnimInstance && PickMontage)
		{
			AnimInstance->Montage_Play(PickMontage);
			AnimInstance->Montage_JumpToSection(SelectionName,PickMontage);
		}
	}
}

bool AShootCharacter::PlayGatheringMontage()
{
	// 手持物品时也允许采集动画（旧逻辑导致拿着东西采瞬间完成、无动画）
	if (CharacterActionState == ECharacterActionState::EAS_Unoccupied
		|| CharacterActionState == ECharacterActionState::EAS_Picking)
	{
		UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
		if (AnimInstance && GatheringMontage)
		{
			AnimInstance->Montage_Play(GatheringMontage);
			return true;
		}
	}
	return false;
}

bool AShootCharacter::PlayWateringMontage()
{
	// 手持物品时也允许浇水动画（旧逻辑导致拿着东西浇水瞬间完成、无动画）
	if (CharacterActionState == ECharacterActionState::EAS_Unoccupied
		|| CharacterActionState == ECharacterActionState::EAS_Picking)
	{
		UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
		if (AnimInstance && WateringMontage)
		{
			AnimInstance->Montage_Play(WateringMontage);
			return true;
		}
	}
	return false;
}

void AShootCharacter::GatheringMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// 兜底定时器到此作废（正常路径由蒙太奇结束触发本函数）
	GetWorldTimerManager().ClearTimer(GatheringDoneTimerHandle);

	// 采集动画播完（无论是否被中断）才真正收获并删除植物
	if (PendingGatherPlant && IsValid(PendingGatherPlant))
	{
		PendingGatherPlant->Harvest();
	}
	PendingGatherPlant = nullptr;
	bHasInteractFaceRotation = false;

	// 蒙太奇若被误配为“循环”，此处强制停止，避免角色一直停留在采集动画
	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		AnimInstance->Montage_Stop(0.25f, GatheringMontage);
	}
}

void AShootCharacter::WateringMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// 兜底定时器到此作废（正常路径由蒙太奇结束触发本函数，避免双触发重复执行）
	GetWorldTimerManager().ClearTimer(WateringDoneTimerHandle);

	// 浇水动画播完（无论是否被中断，或由定时器兜底触发）才真正补水；
	// 描边由 APlant::UpdateHarvestHintOutline 在补水后（不再需要浇水时）自动清除，此处不再手动处理
	if (PendingWaterPlant && IsValid(PendingWaterPlant))
	{
		PendingWaterPlant->Water();
		PendingWaterPlant->SetWaterHintSuppressed(true); // 浇完后取消高亮（植株变干或离开范围后恢复）
	}
	PendingWaterPlant = nullptr;
	bHasInteractFaceRotation = false;

	// 蒙太奇若被误配为“循环”，此处强制停止，避免角色一直停留在浇水动画
	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		AnimInstance->Montage_Stop(0.25f, WateringMontage);
	}
}

 bool AShootCharacter::CanAttack()
{
	return CharacterActionState == ECharacterActionState::EAS_Unoccupied &&
			CharacterState != ECharacterState::ECS_Holding &&
			CharacterState != ECharacterState::ECS_Repairing ;
}

bool AShootCharacter::IsHolding()
{
	return CharacterState == ECharacterState::ECS_Holding;
}

 bool AShootCharacter::CanPick()
 {
 	if(OverlappingItem == nullptr)
 	{
 		return false;
 	}
	if (Cast<ADesk>(OverlappingItem))//Desk（上菜台）不可被拾取
	{
		return false;
	}
	return true;
 }

bool AShootCharacter::IsSpawner()
{
	// 生成箱现已抽取为独立的 AFoodSpawner 类：直接按类型判断（取代原 EIT_Spawn 枚举分支）
	return Cast<AFoodSpawner>(OverlappingItem) != nullptr;
}

 bool AShootCharacter::IsSpawnOther()
 {
 	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::White, FString::Printf(TEXT("切好菜了，生成一个对应切换的item %d"),CookingMachine->FoodActors.Num()));
 	for (int32 i = 0; i < CookingMachine->FoodActors.Num(); i++)
 	{
		AItemActor* CurrentActor = CookingMachine->FoodActors[i];
		if (Cast<AFoodSpawnOther>(CurrentActor))//切菜生成物（原 EIT_SpawnOther）
		{
			return true;
		}
 	}
 	return false;
 }

 bool AShootCharacter::NeedDropDown()
 {
 	return PickingItem != nullptr;
 }

 bool AShootCharacter::CanThrowToMachine()
 {
	return CharacterState == ECharacterState::ECS_Holding && CookingMachine!= nullptr &&
			CharacterState != ECharacterState::ECS_Repairing &&
				Cast<ADish>(PickingItem) == nullptr;//碟子不能丢进机器
}

 bool AShootCharacter::NeedRepairMachine()
 {
	return CharacterState == ECharacterState::ECS_Idle && CookingMachine!= nullptr && CookingMachine->OnFire;
 }

 bool AShootCharacter::IsRepairing()
 {
	return CharacterState == ECharacterState::ECS_Repairing;
 }

 bool AShootCharacter::MachineCookFinished()
 {
 // 	if (PickingItem == nullptr || PickingItem->ItemType != EItemType::EIT_Dish)
 // 	{
 // 		return false;	
 // 	}
	// return CharacterState == ECharacterState::ECS_Holding && CookingMachine!= nullptr && CookingMachine->IsFinish;

 	//快速验证
 	return CookingMachine!= nullptr && CookingMachine->IsFinish;
 }

 bool AShootCharacter::CanGiveDishToGuest()
 {
 	if (PickingItem == nullptr || OverlappingItem == nullptr || CharacterState != ECharacterState::ECS_Holding)
 	{
 		return false;	
 	}
 	if (PickingItem->FoodIdArray.Num()==0)
 	{
 		return false;	 	}
	if (Cast<ADesk>(OverlappingItem) == nullptr)//上菜目标必须是 Desk（上菜台）
	{
		return false;	
	}
	return true;
}

 AShootCharacter::AShootCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, WalkRotationRate, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	// 手感调参：快起快落（马里奥式跳跃），更高加速让起步跟手，冲刺提速见 RushStart/RushEnd
	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->AirControl = 0.5f;
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	GetCharacterMovement()->MaxAcceleration = WalkAcceleration;
	GetCharacterMovement()->GravityScale = 2.0f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = false; // 镜头不与被遮挡的场景物件发生碰撞拉近距离
	// 相机延迟跟随：保留一点缓冲吸收台阶/落地的上下颠簸，
	// 但 LagSpeed 固定不变、并限制最大滞后距离，避免加速/急停时镜头前后抽动
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = CameraLagSpeed;
	CameraBoom->CameraLagMaxDistance = CameraLagMaxDistance;
	// 旋转不做滞后：鼠标转视角必须即时响应
	CameraBoom->bEnableCameraRotationLag = false;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	TargetFOV = FollowCamera->FieldOfView; // 冲刺 FOV 插值的基准

	// 冲刺 Input Action 直接在 C++ 里加载，免去每个角色蓝图手动赋值
	static ConstructorHelpers::FObjectFinder<UInputAction> RushActionFinder(TEXT("/Game/Input/Actions/IA_Rush"));
	if (RushActionFinder.Succeeded())
	{
		RushAction = RushActionFinder.Object;
	}

 	RetargetMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RetargetMesh"));
 	RetargetMesh->SetupAttachment(GetMesh());

 
}

void AShootCharacter::SetOverlappingItem(AItemActor* Item)
{
 	if (IsPicking)
 	{
 		return ;
 	}

 	if (OverlappingItem != nullptr && Item != nullptr && OverlappingItem != Item
 		&& OverlappingItem->GetItemState() != EItemState::EIS_Equipped
 		&& OverlappingItem->GetItemState() != EItemState::EIS_Cooking)
 	{
 		OverlappingItem->SetItemState(EItemState::EIS_Idle);
 	}
	OverlappingItem = Item;

	// 新目标若处于 Idle（曾是候选但被切走），恢复 Hovering 让描边重新点亮
	if (Item && Item->GetItemState() == EItemState::EIS_Idle
		&& Item->GetItemState() != EItemState::EIS_Equipped)
	{
		Item->SetItemState(EItemState::EIS_Hovering);
	}

	// 驱动 HUD：对准食物箱时显示其剩余数量，否则隐藏（带缓存，避免每帧重复推送）
	if (UHUDWidget* HUD = GetHUDWidget())
	{
		if (AFoodSpawner* Spawner = Cast<AFoodSpawner>(Item))
		{
			const int32 Cnt = Spawner->RemainingCount;
			if (Spawner != LastSpawnerForHUD || Cnt != LastSpawnerCount)
			{
				HUD->SetSpawnerRemaining(Cnt);
				LastSpawnerForHUD = Spawner;
				LastSpawnerCount = Cnt;
			}
		}
		else if (LastSpawnerForHUD != nullptr)
		{
			HUD->HideSpawnerRemaining();
			LastSpawnerForHUD = nullptr;
			LastSpawnerCount = -2;
		}
	}
}

void AShootCharacter::AddOverlappingItem(AItemActor* Item)
{
	if (IsPicking || !Item || !IsValid(Item))
	{
		return;
	}
	// 已被拿在手上/正在锅里烹饪的不进入候选集
	if (Item->GetItemState() == EItemState::EIS_Equipped || Item->GetItemState() == EItemState::EIS_Cooking)
	{
		return;
	}
	if (!OverlappingItems.Contains(Item))
	{
		OverlappingItems.Add(Item);
	}
	RefreshBestOverlappingItem();
}

void AShootCharacter::RemoveOverlappingItem(AItemActor* Item)
{
	if (!Item)
	{
		return;
	}
	OverlappingItems.RemoveSingle(Item);
	// 该物品刚被移除（自身已置 Idle 或即将置 Idle），直接重选即可
	RefreshBestOverlappingItem();
}

void AShootCharacter::RefreshBestOverlappingItem()
{
	// 清理失效引用（物品被销毁/入包等）
	for (int32 i = OverlappingItems.Num() - 1; i >= 0; --i)
	{
		AItemActor* It = OverlappingItems[i];
		if (!It || !IsValid(It)
			|| It->GetItemState() == EItemState::EIS_Equipped
			|| It->GetItemState() == EItemState::EIS_Cooking)
		{
			OverlappingItems.RemoveAtSwap(i);
		}
	}

	// 选"朝向加权距离"最小者作为当前目标：同等距离下正前方的优先，
	// 背后的食材再近也排在后面（否则会出现"按 E 却转身去捡背后那颗"）
	AItemActor* Best = nullptr;
	float BestScore = FLT_MAX;
	for (AItemActor* It : OverlappingItems)
	{
		// 视线被墙/台面挡住的不算候选（范围够但看不见 → 不该按 E 生效）
		if (!HasInteractionLineOfSight(It))
		{
			continue;
		}
		const float Score = GetFacingWeightedDistance(It);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = It;
		}
	}

	SetOverlappingItem(Best);
}

bool AShootCharacter::HasInteractionLineOfSight(const AActor* Target) const
{
	if (!Target || !GetWorld())
	{
		return false;
	}

	// 眼睛位置取胶囊中心偏上：贴地目标不会因为地面本身被误判成遮挡
	const FVector EyeLoc = GetActorLocation() + FVector(0.f, 0.f, 60.f);

	// 终点取目标包围盒上离眼睛最近的点 —— 用 Actor 中心的话，
	// 大件物体（灶台）会先撞到它自己的网格，导致永远“被遮挡”
	FVector TargetPoint = Target->GetActorLocation();
	const FBox Bounds = Target->GetComponentsBoundingBox(true);
	if (Bounds.IsValid)
	{
		TargetPoint = Bounds.GetClosestPointTo(EyeLoc);
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(InteractionLOS), false, this);
	Params.AddIgnoredActor(Target);

	FHitResult Hit;
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, EyeLoc, TargetPoint, ECC_Visibility, Params);
	if (!bBlocked)
	{
		return true;
	}

	// 命中点贴着目标时不算遮挡：薄物件擦边、或者视线正好掠过桌面/椅背边缘，
	// 这类"擦到一点点"的命中不该判成"看不见"，否则玩家会觉得贴着桌子也交互不了
	if (Bounds.IsValid && Bounds.ExpandBy(30.f).IsInside(Hit.ImpactPoint))
	{
		return true;
	}
	return FVector::Dist(Hit.ImpactPoint, TargetPoint) < 25.f;
}

// ============ 灶台候选集 ============
void AShootCharacter::AddOverlappingCookMachine(ACookingMachine* Machine)
{
	if (!IsValid(Machine))
	{
		return;
	}
	OverlappingCookMachines.AddUnique(Machine);
	RefreshBestCookMachine();
}

void AShootCharacter::RemoveOverlappingCookMachine(ACookingMachine* Machine)
{
	if (!Machine)
	{
		return;
	}
	OverlappingCookMachines.Remove(Machine);
	RefreshBestCookMachine();
}

void AShootCharacter::RefreshBestCookMachine()
{
	// 清理失效引用（灶台被销毁/换关卡残留）
	for (int32 i = OverlappingCookMachines.Num() - 1; i >= 0; --i)
	{
		if (!IsValid(OverlappingCookMachines[i]))
		{
			OverlappingCookMachines.RemoveAtSwap(i);
		}
	}

	// 选“视线可达 + 距离最近”的一台
	ACookingMachine* Best = nullptr;
	float BestDistSq = FLT_MAX;
	const FVector MyLoc = GetActorLocation();
	for (ACookingMachine* M : OverlappingCookMachines)
	{
		if (!HasInteractionLineOfSight(M))
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(MyLoc, M->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = M;
		}
	}

	if (CookingMachine != Best)
	{
		CookingMachine = Best;
	}
}

// ============ 植物候选集 ============
void AShootCharacter::AddOverlappingPlant(APlant* Plant)
{
	if (!IsValid(Plant))
	{
		return;
	}
	OverlappingPlants.AddUnique(Plant);
	RefreshBestPlant();
}

void AShootCharacter::RemoveOverlappingPlant(APlant* Plant)
{
	if (!Plant)
	{
		return;
	}
	OverlappingPlants.Remove(Plant);
	RefreshBestPlant();
}

void AShootCharacter::RefreshBestPlant()
{
	for (int32 i = OverlappingPlants.Num() - 1; i >= 0; --i)
	{
		if (!IsValid(OverlappingPlants[i]))
		{
			OverlappingPlants.RemoveAtSwap(i);
		}
	}

	APlant* Best = nullptr;
	float BestScore = FLT_MAX;
	for (APlant* P : OverlappingPlants)
	{
		if (!HasInteractionLineOfSight(P))
		{
			continue;
		}
		// 朝向加权：正前方的植株优先，背后的再近也不抢
		const float Score = GetFacingWeightedDistance(P);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = P;
		}
	}

	// 交给 SetOverlappingPlant 统一处理描边刷新（新旧目标的 Outline 都会重估）
	SetOverlappingPlant(Best);
}

namespace
{
	/** 交互候选排序：Tier 优先 → 朝向加权距离 → 真实距离。
	 *  Pick() 与 RefreshInteractionHighlight() 共用同一份规则，
	 *  保证"亮着的那个"就是"按 E 会生效的那个"。 */
	inline bool InteractionOptionLess(const AShootCharacter::FInteractOption& A, const AShootCharacter::FInteractOption& B)
	{
		if (A.Tier != B.Tier)
		{
			return A.Tier < B.Tier;
		}
		if (!FMath::IsNearlyEqual(A.Score, B.Score, 0.5f))
		{
			return A.Score < B.Score;
		}
		return A.Dist < B.Dist;
	}

	/** 通用候选目标重选：先剔除失效项，再返回"视线可达 + 朝向加权距离最小"的一个。
	 *  ScoreOf 由调用方传入（需要角色朝向），HasLos 同理由调用方传入——
	 *  两者都依赖角色实例（眼睛位置/脸朝向/世界），无法做成静态工具。 */
	template <typename T, typename FScorePredicate, typename FLosPredicate>
	T* PickBestInteractionCandidate(TArray<TObjectPtr<T>>& Candidates, FScorePredicate&& ScoreOf, FLosPredicate&& HasLos)
	{
		for (int32 i = Candidates.Num() - 1; i >= 0; --i)
		{
			if (!IsValid(Candidates[i]))
			{
				Candidates.RemoveAtSwap(i);
			}
		}

		T* Best = nullptr;
		float BestScore = FLT_MAX;
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			T* Candidate = Candidates[i];
			if (!Candidate || !HasLos(Candidate))
			{
				continue;
			}
			const float Score = ScoreOf(Candidate);
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = Candidate;
			}
		}
		return Best;
	}
}

// ============ 上菜台候选集 ============
void AShootCharacter::AddOverlappingCounter(AServingCounter* Counter)
{
	if (!IsValid(Counter))
	{
		return;
	}
	OverlappingServingCounters.AddUnique(Counter);
	RefreshBestCounter();
}

void AShootCharacter::RemoveOverlappingCounter(AServingCounter* Counter)
{
	if (!Counter)
	{
		return;
	}
	OverlappingServingCounters.Remove(Counter);
	RefreshBestCounter();
}

void AShootCharacter::RefreshBestCounter()
{
	OverlappingCounter = PickBestInteractionCandidate(OverlappingServingCounters,
		[this](const AActor* A) { return GetFacingWeightedDistance(A); },
		[this](const AActor* A) { return HasInteractionLineOfSight(A); });
}

// ============ 洗碗机候选集 ============
void AShootCharacter::AddOverlappingDishwasher(ADishwasher* Dishwasher)
{
	if (!IsValid(Dishwasher))
	{
		return;
	}
	OverlappingDishwashers.AddUnique(Dishwasher);
	RefreshBestDishwasher();
}

void AShootCharacter::RemoveOverlappingDishwasher(ADishwasher* Dishwasher)
{
	if (!Dishwasher)
	{
		return;
	}
	OverlappingDishwashers.Remove(Dishwasher);
	RefreshBestDishwasher();
}

void AShootCharacter::RefreshBestDishwasher()
{
	OverlappingDishwasher = PickBestInteractionCandidate(OverlappingDishwashers,
		[this](const AActor* A) { return GetFacingWeightedDistance(A); },
		[this](const AActor* A) { return HasInteractionLineOfSight(A); });
}

// ============ 招揽台候选集 ============
void AShootCharacter::AddOverlappingAttractCounter(AAttractCounter* Counter)
{
	if (!IsValid(Counter))
	{
		return;
	}
	OverlappingAttractCounters.AddUnique(Counter);
	RefreshBestAttractCounter();
}

void AShootCharacter::RemoveOverlappingAttractCounter(AAttractCounter* Counter)
{
	if (!Counter)
	{
		return;
	}
	OverlappingAttractCounters.Remove(Counter);
	RefreshBestAttractCounter();
}

void AShootCharacter::RefreshBestAttractCounter()
{
	OverlappingAttractCounter = PickBestInteractionCandidate(OverlappingAttractCounters,
		[this](const AActor* A) { return GetFacingWeightedDistance(A); },
		[this](const AActor* A) { return HasInteractionLineOfSight(A); });
}

// ============ 大门交互台候选集 ============
void AShootCharacter::AddOverlappingGate(AGateCounter* Gate)
{
	if (!IsValid(Gate))
	{
		return;
	}
	OverlappingGates.AddUnique(Gate);
	RefreshBestGate();
}

void AShootCharacter::RemoveOverlappingGate(AGateCounter* Gate)
{
	if (!Gate)
	{
		return;
	}
	OverlappingGates.Remove(Gate);
	RefreshBestGate();
}

void AShootCharacter::RefreshBestGate()
{
	OverlappingGate = PickBestInteractionCandidate(OverlappingGates,
		[this](const AActor* A) { return GetFacingWeightedDistance(A); },
		[this](const AActor* A) { return HasInteractionLineOfSight(A); });
}

// ============ 客人候选集 ============
void AShootCharacter::AddOverlappingGuest(AGuestCharacter* Guest)
{
	if (!IsValid(Guest))
	{
		return;
	}
	OverlappingGuests.AddUnique(Guest);
	RefreshBestGuest();
}

void AShootCharacter::RemoveOverlappingGuest(AGuestCharacter* Guest)
{
	if (!Guest)
	{
		return;
	}
	OverlappingGuests.Remove(Guest);
	RefreshBestGuest();
}

void AShootCharacter::RefreshBestGuest()
{
	OverlappingGuest = PickBestInteractionCandidate(OverlappingGuests,
		[this](const AActor* A) { return GetFacingWeightedDistance(A); },
		[this](const AActor* A) { return HasInteractionLineOfSight(A); });
}

// ============ 餐桌候选集 ============
void AShootCharacter::AddOverlappingChairSeat(AChairSeat* Seat)
{
	if (!IsValid(Seat))
	{
		return;
	}
	OverlappingChairSeats.AddUnique(Seat);
	RefreshBestChairSeat();
}

void AShootCharacter::RemoveOverlappingChairSeat(AChairSeat* Seat)
{
	if (!Seat)
	{
		return;
	}
	OverlappingChairSeats.Remove(Seat);
	RefreshBestChairSeat();
}

void AShootCharacter::RefreshBestChairSeat()
{
	OverlappingChairSeat = PickBestInteractionCandidate(OverlappingChairSeats,
		[this](const AActor* A) { return GetFacingWeightedDistance(A); },
		[this](const AActor* A) { return HasInteractionLineOfSight(A); });
}

// ============ 研究炉候选集 ============
void AShootCharacter::AddOverlappingResearchStove(AResearchStove* Stove)
{
	if (!IsValid(Stove))
	{
		return;
	}
	OverlappingResearchStoves.AddUnique(Stove);
	RefreshBestResearchStove();
}

void AShootCharacter::RemoveOverlappingResearchStove(AResearchStove* Stove)
{
	if (!Stove)
	{
		return;
	}
	OverlappingResearchStoves.Remove(Stove);
	RefreshBestResearchStove();
}

void AShootCharacter::RefreshBestResearchStove()
{
	OverlappingResearchStove = PickBestInteractionCandidate(OverlappingResearchStoves,
		[this](const AActor* A) { return GetFacingWeightedDistance(A); },
		[this](const AActor* A) { return HasInteractionLineOfSight(A); });
}

// ============ 空碟候选集 ============
void AShootCharacter::AddOverlappingEmptyPlate(AItemActor* Plate)
{
	if (!IsValid(Plate))
	{
		return;
	}
	OverlappingEmptyPlates.AddUnique(Plate);
	RefreshBestEmptyPlate();
}

void AShootCharacter::RemoveOverlappingEmptyPlate(AItemActor* Plate)
{
	if (!Plate)
	{
		return;
	}
	OverlappingEmptyPlates.Remove(Plate);
	RefreshBestEmptyPlate();
}

void AShootCharacter::RefreshBestEmptyPlate()
{
	OverlappingEmptyPlate = PickBestInteractionCandidate(OverlappingEmptyPlates,
		[this](const AActor* A) { return GetFacingWeightedDistance(A); },
		[this](const AActor* A) { return HasInteractionLineOfSight(A); });
}

// ============ 仓库候选集 ============
void AShootCharacter::AddOverlappingWarehouse(AStorageWarehouse* Warehouse)
{
	if (!IsValid(Warehouse))
	{
		return;
	}
	OverlappingWarehouses.AddUnique(Warehouse);
	RefreshBestWarehouse();
}

void AShootCharacter::RemoveOverlappingWarehouse(AStorageWarehouse* Warehouse)
{
	if (!Warehouse)
	{
		return;
	}
	OverlappingWarehouses.Remove(Warehouse);
	RefreshBestWarehouse();
}

void AShootCharacter::RefreshBestWarehouse()
{
	OverlappingWarehouse = PickBestInteractionCandidate(OverlappingWarehouses,
		[this](const AActor* A) { return GetFacingWeightedDistance(A); },
		[this](const AActor* A) { return HasInteractionLineOfSight(A); });
}

// ============ 对话目标候选集 ============
// 对话是组件而不是 Actor，距离/视线都以它宿主 Actor 为基准
void AShootCharacter::AddOverlappingDialogue(UInteractionDialogueComponent* Comp)
{
	if (!IsValid(Comp))
	{
		return;
	}
	OverlappingDialogues.AddUnique(Comp);
	RefreshBestDialogue();
}

void AShootCharacter::RemoveOverlappingDialogue(UInteractionDialogueComponent* Comp)
{
	if (!Comp)
	{
		return;
	}
	OverlappingDialogues.Remove(Comp);
	RefreshBestDialogue();
}

void AShootCharacter::RefreshBestDialogue()
{
	for (int32 i = OverlappingDialogues.Num() - 1; i >= 0; --i)
	{
		if (!IsValid(OverlappingDialogues[i]))
		{
			OverlappingDialogues.RemoveAtSwap(i);
		}
	}

	UInteractionDialogueComponent* Best = nullptr;
	float BestScore = FLT_MAX;
	for (int32 i = 0; i < OverlappingDialogues.Num(); ++i)
	{
		UInteractionDialogueComponent* Comp = OverlappingDialogues[i];
		AActor* CompOwner = Comp ? Comp->GetOwner() : nullptr;
		if (!CompOwner || !HasInteractionLineOfSight(CompOwner))
		{
			continue;
		}
		const float Score = GetFacingWeightedDistance(CompOwner);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Comp;
		}
	}
	OverlappingDialogue = Best;
}

void AShootCharacter::RefreshAllInteractionTargets()
{
	// 运行时倍率（Shoot.InteractScale）变了 → 立刻把所有交互球按新倍率重设一遍。
	// 放在最前面：球体尺寸变化触发的重叠事件会被下面这轮重选一并消化。
	const float Scale = InteractionScale::Get();
	if (!FMath::IsNearlyEqual(Scale, LastAppliedInteractionScale))
	{
		LastAppliedInteractionScale = Scale;
		InteractionScale::RefreshAllInteractionScales(GetWorld());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan,
				FString::Printf(TEXT("[交互] 半径倍率 %.2f"), Scale));
		}
	}

	RefreshBestOverlappingItem();
	RefreshBestCookMachine();
	RefreshBestPlant();

	// 其余交互目标同样定期重估：绕到墙后、走到两台设备中间、目标被搬走都能及时反映
	RefreshBestCounter();
	RefreshBestDishwasher();
	RefreshBestAttractCounter();
	RefreshBestGate();
	RefreshBestGuest();
	RefreshBestChairSeat();
	RefreshBestResearchStove();
	RefreshBestEmptyPlate();
	RefreshBestWarehouse();
	RefreshBestDialogue();

	// 所有候选都重估完，再统一决定"只亮哪一个"
	RefreshInteractionHighlight();
}

bool AShootCharacter::IsDropLockActive(AItemActor* Item) const
{
	return Item && Item == LastDroppedItem
		&& GetWorld()
		&& (GetWorld()->GetTimeSeconds() - LastDropTime) < DropRePickLockTime;
}

UHUDWidget* AShootCharacter::GetHUDWidget()
{
	if (CachedHUD && IsValid(CachedHUD))
	{
		return CachedHUD;
	}
	CachedHUD = nullptr;
	// 通过 GameInstance 上的关卡管理器指针获取常驻 HUD（更可靠，且自动随关卡切换刷新）
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()))
	{
		if (GI->curLevelBP)
		{
			CachedHUD = GI->curLevelBP->HUDWidget;
		}
	}
	return CachedHUD;
}

 void AShootCharacter::SetCookMachineItem(ACookingMachine* Item)
{
	CookingMachine = Item;
}

void AShootCharacter::SetOverlappingPlant(APlant* Plant)
{
	// 若切换了目标植被，先让旧植被重新评估描边（会因其不再是当前目标而关闭描边）
	if (OverlappingPlant && OverlappingPlant != Plant && IsValid(OverlappingPlant))
	{
		OverlappingPlant->UpdateHarvestHintOutline();
	}

	OverlappingPlant = Plant;

	// 新目标植被立即评估描边（若满足条件则点亮，否则保持关闭）
	if (Plant && IsValid(Plant))
	{
		Plant->UpdateHarvestHintOutline();
	}
}

void AShootCharacter::SetOverlappingCounter(AServingCounter* Counter)
{
	OverlappingCounter = Counter;
}

void AShootCharacter::SetOverlappingAttractCounter(AAttractCounter* Counter)
{
	OverlappingAttractCounter = Counter;
}

void AShootCharacter::SetOverlappingGate(AGateCounter* Gate)
{
	OverlappingGate = Gate;
}

void AShootCharacter::SetOverlappingGuest(AGuestCharacter* Guest)
{
	OverlappingGuest = Guest;
}

void AShootCharacter::SetOverlappingChairSeat(AChairSeat* Seat)
{
	OverlappingChairSeat = Seat;
}

void AShootCharacter::ClearHeldItemState()
{
	// 物品已经交给别人（上菜 / 交还等）：清空手持主物体并复位动作与手部 IK
	SetPickingItem(nullptr);
	CharacterActionState = ECharacterActionState::EAS_Unoccupied;
	CharacterState = ECharacterState::ECS_Idle;
	FBIK_Alpha = 0;
	HandIK_LPoint = nullptr;
	HandIK_RPoint = nullptr;
}

void AShootCharacter::SetOverlappingDishwasher(ADishwasher* Dishwasher)
{
	OverlappingDishwasher = Dishwasher;
}

void AShootCharacter::SetOverlappingEmptyPlate(AItemActor* Plate)
{
	OverlappingEmptyPlate = Plate;
}

void AShootCharacter::SetOverlappingResearchStove(AResearchStove* Stove)
{
	OverlappingResearchStove = Stove;
}

void AShootCharacter::SetOverlappingWarehouse(AStorageWarehouse* Warehouse)
{
	OverlappingWarehouse = Warehouse;
}

void AShootCharacter::SetOverlappingDialogue(UInteractionDialogueComponent* Comp)
{
	OverlappingDialogue = Comp;
}

void AShootCharacter::InteractWithPlant()
{
	if (!OverlappingPlant)
	{
		return;
	}

	// 上下文动作：优先处理害虫，其次收获，最后浇水
	if (OverlappingPlant->HasPest())
	{
		OverlappingPlant->RemovePest();
	}
	else if (OverlappingPlant->IsFruiting())
	{
		// 触发摘取植被：先播放 A_Gathering，待动画播完（OnGatheringMontageEnded）后再真正收获并删除植物
		if (PendingGatherPlant == nullptr)
		{
			PendingGatherPlant = OverlappingPlant;

			// 采集时朝向植被（开关控制）：仅水平转向，由 Tick 里的 RInterpTo 平滑逼近（不再瞬时转身）
			if (bFacePlantWhenGathering && PendingGatherPlant)
			{
				FVector ToPlant = PendingGatherPlant->GetActorLocation() - GetActorLocation();
				ToPlant.Z = 0.f;
				if (!ToPlant.IsNearlyZero())
				{
					InteractFaceRotation = ToPlant.Rotation();
					bHasInteractFaceRotation = true;
				}
			}

			if (PlayGatheringMontage())
			{
				if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
				{
					OnGatheringMontageEnded.BindUObject(this, &AShootCharacter::GatheringMontageEnded);
					AnimInstance->Montage_SetEndDelegate(OnGatheringMontageEnded, GatheringMontage);
				}
				else
				{
					// 没有动画实例：直接收获
					PendingGatherPlant->Harvest();
					PendingGatherPlant = nullptr;
				}

				// 兜底定时器：以蒙太奇一轮时长为准到点强制完成采集。
				// 作用：即使采集蒙太奇被误配为“循环”或被中断（自然结束事件不触发），
				// 也能在播放一轮后收获并停止蒙太奇，避免 PendingGatherPlant 永不置空吞掉后续采摘。
				const float GatherDuration = (GatheringMontage && GatheringMontage->GetPlayLength() > 0.f)
					? GatheringMontage->GetPlayLength()
					: 1.f;
				FTimerDelegate GatherDoneDel;
				GatherDoneDel.BindUObject(this, &AShootCharacter::GatheringMontageEnded, (UAnimMontage*)nullptr, false);
				GetWorldTimerManager().SetTimer(GatheringDoneTimerHandle, GatherDoneDel, GatherDuration, false);
			}
			else
			{
				// 蒙太奇未播放（未配置）：直接收获
				PendingGatherPlant->Harvest();
				PendingGatherPlant = nullptr;
			}
		}
	}
	else if (OverlappingPlant->NeedsWater())
	{
		// 浇水时朝向植被（复用采集时的开关控制）：仅水平转向，由 Tick 平滑逼近
		if (bFacePlantWhenGathering && OverlappingPlant)
		{
			FVector ToPlant = OverlappingPlant->GetActorLocation() - GetActorLocation();
			ToPlant.Z = 0.f;
			if (!ToPlant.IsNearlyZero())
			{
				InteractFaceRotation = ToPlant.Rotation();
				bHasInteractFaceRotation = true;
			}
		}

		// 同一株植物正在等待浇水动画时不再重复触发
		if (PendingWaterPlant == nullptr)
		{
			// 注：植被高亮改由 APlant::UpdateHarvestHintOutline 在“玩家靠近且需要浇水”时处理（浇水前置指示），
			// 此处不再手动描边，避免与植物自身描边缓存冲突。

			// 触发浇水：先播放浇水动画，待动画播完（OnWateringMontageEnded）后才真正补水
			if (PlayWateringMontage())
			{
				PendingWaterPlant = OverlappingPlant;
				if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
				{
					OnWateringMontageEnded.BindUObject(this, &AShootCharacter::WateringMontageEnded);
					AnimInstance->Montage_SetEndDelegate(OnWateringMontageEnded, WateringMontage);
				}

				// 兜底定时器：以蒙太奇一轮时长为准到点强制完成浇水。
				// 作用：即使浇水蒙太奇被误配为“循环”（自然结束事件永不触发），
				// 也能在播放一轮后补水并停止蒙太奇，避免角色一直停留在浇水状态。
				const float Duration = (WateringMontage && WateringMontage->GetPlayLength() > 0.f)
					? WateringMontage->GetPlayLength()
					: 1.f;
				FTimerDelegate WateringDoneDel;
				WateringDoneDel.BindUObject(this, &AShootCharacter::WateringMontageEnded, (UAnimMontage*)nullptr, false);
				GetWorldTimerManager().SetTimer(WateringDoneTimerHandle, WateringDoneDel, Duration, false);
			}
			else
			{
				// 蒙太奇未播放（未配置或角色忙碌）：直接补水（描边由植物自身逻辑管理）
				OverlappingPlant->Water();
				OverlappingPlant->SetWaterHintSuppressed(true);
			}
		}
	}
}

void AShootCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AShootCharacter::Move);

		// 保留左右旋转（Yaw），禁止上下旋转（Pitch）
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AShootCharacter::Look);
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AShootCharacter::Look);

		// Attack
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Triggered, this, &AShootCharacter::Attack);

		// Pick：拾取；靠近植物时 Pick 内部会转为浇水/驱虫/收获（见 AShootCharacter::Pick）
		// 注意：使用 Started（按下瞬间触发一次），不要用 Triggered（按住期间每帧都触发），
		// 否则按住 E 时浇水/采集动画播完会立刻被重新触发，表现为“一直循环”，对话也会反复开关。
		EnhancedInputComponent->BindAction(PickAction, ETriggerEvent::Started, this, &AShootCharacter::Pick);

		// E 键松开：仅用于结束"按住范围拾取"（长按吸 2m 内的散落物）。
		// 拾取本身仍然只在 Started 触发一次，避免 Triggered 每帧重放导致动画循环。
		EnhancedInputComponent->BindAction(PickAction, ETriggerEvent::Completed, this, &AShootCharacter::PickReleased);

		//PushFood
		EnhancedInputComponent->BindAction(PushFoodAction, ETriggerEvent::Started, this, &AShootCharacter::PushFoodStart);
		EnhancedInputComponent->BindAction(PushFoodAction, ETriggerEvent::Completed, this, &AShootCharacter::PushFoodComplete);


		EnhancedInputComponent->BindAction(WheelAction, ETriggerEvent::Triggered, this, &AShootCharacter::OnMouseWheelAxis);

		// 放入背包 (T 键)：把当前手上的物品收进背包
		EnhancedInputComponent->BindAction(StoreToInventoryAction, ETriggerEvent::Triggered, this, &AShootCharacter::StoreHeldItemToInventory);

		// 从背包取出(生成到手上)：取出第一个物品
		EnhancedInputComponent->BindAction(TakeOutInventoryAction, ETriggerEvent::Triggered, this, &AShootCharacter::TakeOutFirstInventoryItem);

		// 打开/关闭背包界面 (B 键)：用 Completed(松开时触发一次)，避免 Triggered 在按住期间反复触发导致快速开合闪烁
		EnhancedInputComponent->BindAction(OpenInventoryAction, ETriggerEvent::Completed, this, &AShootCharacter::ToggleInventory);

		// 打开/关闭商店界面 (P 键)
		EnhancedInputComponent->BindAction(OpenShopAction, ETriggerEvent::Completed, this, &AShootCharacter::ToggleShop);

		// 冲刺 (LeftShift)：按住提速 + 相机 FOV 拉大做速度感，松开恢复
		if (RushAction)
		{
			EnhancedInputComponent->BindAction(RushAction, ETriggerEvent::Started, this, &AShootCharacter::RushStart);
			EnhancedInputComponent->BindAction(RushAction, ETriggerEvent::Completed, this, &AShootCharacter::RushEnd);
		}

	}

	else
	{
		UE_LOG(LogShoot, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AShootCharacter::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

void AShootCharacter::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AShootCharacter::Attack(const FInputActionValue& Value)
{
	if (CanAttack())
	{
		DoAttack();
	}
}

void AShootCharacter::RushStart(const FInputActionValue& Value)
{
	if (bIsSprinting)
	{
		return;
	}
	// 死亡 / 修理 / 攻击中不允许冲刺（这些状态下移动本身受限，强行提速会打架）
	if (CharacterState == ECharacterState::ECS_Dead
		|| CharacterState == ECharacterState::ECS_Repairing
		|| CharacterActionState == ECharacterActionState::EAS_Attacking)
	{
		return;
	}

	bIsSprinting = true;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = SprintSpeed;
	Move->MaxAcceleration = SprintAcceleration;
	Move->RotationRate = FRotator(0.f, SprintRotationRate, 0.f);

	// 起步脉冲默认关闭：速度阶跃（600→900 一帧完成）会让弹簧臂相机先被甩开再追上，
	// 视觉上就是镜头猛地往前一突。现在靠 SprintAcceleration 在 ~0.06s 内平滑到速。
	if (bSprintInstantBurst && Move->IsMovingOnGround())
	{
		const FVector Dir = FVector(Move->Velocity.X, Move->Velocity.Y, 0.f).GetSafeNormal();
		if (!Dir.IsNearlyZero())
		{
			Move->Velocity = Dir * SprintSpeed;
		}
	}

	TargetFOV = SprintFOV; // FOV 与相机跟随在 Tick 中平滑逼近
}

void AShootCharacter::RushEnd(const FInputActionValue& Value)
{
	if (!bIsSprinting)
	{
		return;
	}
	bIsSprinting = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = WalkSpeed;
	Move->MaxAcceleration = WalkAcceleration;
	Move->RotationRate = FRotator(0.f, WalkRotationRate, 0.f);

	TargetFOV = NormalFOV; // 松开后靠 BrakingDeceleration 自然回落，不硬切速度
}

void AShootCharacter::Pick(const FInputActionValue& Value)
{
	// 记录"E 正被按住"：长按由 TickRangePick 转为范围拾取（短按仍然只做一次普通交互）。
	// 缓冲补执行的那次不算按住，否则松手事件已经过去，会永远停留在"按住"状态。
	if (!bConsumingBufferedPick)
	{
		bPickHeld = true;
		PickHeldTime = 0.f;
		RangePickCooldown = 0.f;
	}

	// ============================================================================
	// 交互分发：从"19 层写死的 if-else 优先级链"改为"分层 + 同层按距离决胜"。
	//
	// 旧逻辑的痛点：优先级是硬编码的，只要站在大门/招揽台/餐桌的判定球内，
	// 按 E 永远只做那件事，脚边的食物根本捡不起来；锅煮好时同理（取菜永远压过拾取）。
	//
	// 新逻辑：把所有"此刻可执行"的交互都收集成候选，分四层：
	//   Tier 0  必须响应      —— 手上脏碟（只能交还洗碗机，禁止放下）、已打开的对话
	//   Tier 1  交付手持物品   —— 上菜台 / 端给客人 / 投入研究炉
	//   Tier 2  环境交互       —— 门 / 招揽 / 餐桌 / 仓库 / 植物 / 出锅 / 修理 / 生成箱 / 拾取
	//                            同层内按"角色到目标包围盒最近点"的距离决胜，谁近谁生效
	//   Tier 3  兜底          —— 放下手上的东西
	// 排序后依次尝试，第一个"消费了这次按键"的动作生效；返回 false 表示这次没做成，
	// 让下一个候选接手（例如研究炉拒绝了无效食材，就继续尝试拾取脚下的东西）。
	// ============================================================================
	// ---- 交互动画播放中：记一次缓冲，动画结束后补执行 ----
	// 旧行为：动画期间按 E 完全被吞（Pick 只在 Started 触发，动画中的按键没人处理）；
	// 若直接放行，又会在动画中途触发"放下→再拾取"，表现为抖动。
	// 缓冲把这次按键延后到动画结束，既不吃键也不打断当前动作。
	if (IsInteractionAnimPlaying())
	{
		BufferPickInput();
		return;
	}

	TArray<FInteractOption, TInlineAllocator<12>> Options;
	BuildInteractionOptions(Options);

	// 分层排序：Tier 小的先；同层按距离近的先；完全同分时保持加入顺序（StableSort）
	Options.StableSort(InteractionOptionLess);

	for (const FInteractOption& Option : Options)
	{
		if (Option.Run())
		{
			if (InteractionDebug::IsEnabled() && GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Cyan,
					FString::Printf(TEXT("[交互] %s  (Tier %d / %.0fcm / 朝向分 %.0f)"),
						*Option.Name, Option.Tier, Option.Dist, Option.Score));
			}
			return;
		}
	}
}

// ============================================================================
// 交互朝向：谁近不算数，还要看脸朝哪
// ============================================================================
FVector AShootCharacter::GetInteractionFacing() const
{
	// 角色是 bOrientRotationToMovement：脸永远朝着移动方向，停下来也保持最后朝向。
	// 这正是玩家心里的"正前方"。控制器/相机朝向只在极端情况下兜底。
	FVector Facing = GetActorForwardVector();
	Facing.Z = 0.f;
	if (!Facing.Normalize())
	{
		if (const AController* C = GetController())
		{
			Facing = C->GetControlRotation().Vector();
			Facing.Z = 0.f;
			Facing.Normalize();
		}
	}
	return Facing.IsNearlyZero() ? FVector::ForwardVector : Facing;
}

float AShootCharacter::GetInteractionDistanceTo(const AActor* Target) const
{
	if (!Target)
	{
		return FLT_MAX;
	}
	const FVector MyLoc = GetActorLocation();
	const FBox B = Target->GetComponentsBoundingBox(true);
	return B.IsValid
		? FVector::Dist(MyLoc, B.GetClosestPointTo(MyLoc))
		: FVector::Dist(MyLoc, Target->GetActorLocation());
}

float AShootCharacter::GetFacingWeightedDistance(const AActor* Target) const
{
	const float Dist = GetInteractionDistanceTo(Target);
	if (!Target || Dist >= FLT_MAX)
	{
		return FLT_MAX;
	}

	const float Weight = InteractionFacing::GetPenaltyScale();
	if (Weight <= 0.f || Dist <= KINDA_SMALL_NUMBER)
	{
		return Dist;
	}

	const FVector MyLoc = GetActorLocation();
	const FBox B = Target->GetComponentsBoundingBox(true);
	const FVector Closest = B.IsValid ? B.GetClosestPointTo(MyLoc) : Target->GetActorLocation();

	FVector ToTarget = Closest - MyLoc;
	ToTarget.Z = 0.f;
	const float Horiz = ToTarget.Size();
	if (Horiz < 30.f)
	{
		// 头顶/脚下：水平方向退化成噪声，硬算角度会随机偏向，直接不做惩罚
		return Dist;
	}

	const float Cos = FVector::DotProduct(ToTarget / Horiz, GetInteractionFacing());
	// 偏离度：正前方 cos=1 → 0；正侧面 cos=0 → 0.5；正后方 cos=-1 → 1
	const float OffFront = FMath::Clamp((1.f - Cos) * 0.5f, 0.f, 1.f);
	return Dist * (1.f + Weight * OffFront);
}

void AShootCharacter::BuildInteractionOptions(TArray<FInteractOption, TInlineAllocator<12>>& OutOptions)
{
	constexpr int32 TierForce = 0;
	constexpr int32 TierHandover = 1;
	constexpr int32 TierWorld = 2;
	constexpr int32 TierFallback = 3;

	// 到目标的交互距离：用包围盒最近点（大件设备不会因为 pivot 远而被算得比小物件远）
	auto DistTo = [this](const AActor* Target) -> float
	{
		return GetInteractionDistanceTo(Target);
	};

	// Dist 只用于显示；真正排序用 Score —— 背后的目标会被按偏离度放大，
	// 于是"背后 40cm 的蔬菜"不会再抢走"正前方 90cm 的蔬菜"。权重来自 Shoot.InteractFacing。
	auto AddOption = [this, &OutOptions](int32 Tier, float Dist, const TCHAR* Name, AActor* Target, TFunction<bool()>&& Run)
	{
		FInteractOption Option;
		Option.Tier = Tier;
		Option.Dist = Dist;
		// 没有具体目标（Tier 0 / 放下）时 Dist 是 0，恒排在同层最前，不需要惩罚
		Option.Score = (Target && Dist > 0.f) ? GetFacingWeightedDistance(Target) : Dist;
		Option.Name = Name;
		Option.Target = Target;
		Option.Run = MoveTemp(Run);
		OutOptions.Add(MoveTemp(Option));
	};

	// ---------------- Tier 0：必须响应 ----------------
	// 手上拿着脏碟：唯一出路是走到洗碗机按 E 交还（可用碟子数 +1）。
	// 禁止放下/丢弃/拿去做菜，否则碟子会丢失导致可用碟子数永远无法恢复，
	// 所以这一层无论距离多远都优先，且一定消费按键。
	if (PickingItem && PickingItem->bIsDirtyPlate)
	{
		AddOption(TierForce, 0.f, TEXT("交还脏碟"), OverlappingDishwasher, [this]()
		{
			if (OverlappingDishwasher && OverlappingDishwasher->SubmitPlate(PickingItem))
			{
				ClearHeldItemState();
				return true;
			}
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow, TEXT("[脏碟] 请走到洗碗机旁按 E 交还清洗"));
			}
			return true;
		});
	}

	// 对话已打开：再按 E 关闭（未打开则在 Tier 2 参与距离竞争）
	if (OverlappingDialogue && OverlappingDialogue->IsDialogueOpen())
	{
		AddOption(TierForce, 0.f, TEXT("关闭对话"), OverlappingDialogue->GetOwner(), [this]()
		{
			OverlappingDialogue->CloseDialogue();
			return true;
		});
	}

	// ---------------- Tier 1：交付手持物品 ----------------
	// 手上拿着东西时，"把手上的东西交出去"比脚边任何东西都更符合意图。
	if (PickingItem && !PickingItem->bIsDirtyPlate)
	{
		if (OverlappingResearchStove)
		{
			AddOption(TierHandover, DistTo(OverlappingResearchStove), TEXT("投入研究炉"), OverlappingResearchStove, [this]()
			{
				if (OverlappingResearchStove->AddFood(PickingItem))
				{
					ClearHeldItemState();
					return true;
				}
				return false; // 研究中/无效食材：不消费按键，交给下一个候选
			});
		}

		if (OverlappingGuest && OverlappingGuest->CanPlayerServeDish())
		{
			AddOption(TierHandover, DistTo(OverlappingGuest), TEXT("端菜给客人"), OverlappingGuest, [this]()
			{
				if (OverlappingGuest->TryServeDish(this, PickingItem))
				{
					ClearHeldItemState();
					return true;
				}
				return false; // 菜不对/订单不匹配：不消费按键
			});
		}

		if (OverlappingCounter)
		{
			AddOption(TierHandover, DistTo(OverlappingCounter), TEXT("提交上菜台"), OverlappingCounter, [this]()
			{
				if (OverlappingCounter->SubmitDish(PickingItem))
				{
					ClearHeldItemState();
				}
				// 与原逻辑一致：站在上菜台范围内按 E 就当作"上菜尝试"，不再落到放下
				return true;
			});
		}
	}

	// ---------------- Tier 2：环境交互（同层按距离决胜） ----------------

	// 植物：浇水 / 驱虫 / 收获
	if (OverlappingPlant)
	{
		AddOption(TierWorld, DistTo(OverlappingPlant), TEXT("植物"), OverlappingPlant, [this]()
		{
			InteractWithPlant();
			return true;
		});
	}

	// 通用对话（未打开时）
	if (OverlappingDialogue && !OverlappingDialogue->IsDialogueOpen())
	{
		AddOption(TierWorld, DistTo(OverlappingDialogue->GetOwner()), TEXT("开始对话"), OverlappingDialogue->GetOwner(), [this]()
		{
			OverlappingDialogue->StartDialogue(this);
			return true;
		});
	}

	// 大门：开门营业
	if (OverlappingGate)
	{
		AddOption(TierWorld, DistTo(OverlappingGate), TEXT("开门营业"), OverlappingGate, [this]()
		{
			OverlappingGate->OpenBusiness();
			return true;
		});
	}

	// 招揽台：招揽顾客
	if (OverlappingAttractCounter)
	{
		AddOption(TierWorld, DistTo(OverlappingAttractCounter), TEXT("招揽顾客"), OverlappingAttractCounter, [this]()
		{
			OverlappingAttractCounter->AttractCustomers();
			return true;
		});
	}

	// 餐桌：点单 / 上菜 / 收空碟
	if (OverlappingChairSeat)
	{
		AddOption(TierWorld, DistTo(OverlappingChairSeat), TEXT("餐桌"), OverlappingChairSeat, [this]()
		{
			return OverlappingChairSeat->TryInteractByPlayer(this);
		});
	}

	// 客人接单（仅"需要玩家接单"模式）
	if (OverlappingGuest && OverlappingGuest->CanAcceptOrderByPlayer())
	{
		AddOption(TierWorld, DistTo(OverlappingGuest), TEXT("接单"), OverlappingGuest, [this]()
		{
			OverlappingGuest->AcceptOrderByPlayer();
			return true;
		});
	}

	// 收走空碟 → 变成手上脏碟
	if (OverlappingEmptyPlate && !PickingItem)
	{
		AItemActor* Plate = OverlappingEmptyPlate;
		AddOption(TierWorld, DistTo(Plate), TEXT("收走空碟"), Plate, [this, Plate]()
		{
			if (Plate->PickUpByPlayer(this))
			{
				// 收走后从候选集注销（物体会转为手持脏碟），立即重选下一个空碟
				RemoveOverlappingEmptyPlate(Plate);
				return true;
			}
			return false;
		});
	}

	// 研究炉：空手 + 炉内有残留食材 + 未在研究 → 清空炉内食材（防止投错食材卡住）
	if (OverlappingResearchStove && !PickingItem
		&& !OverlappingResearchStove->bResearching && OverlappingResearchStove->StoredItemIDs.Num() > 0)
	{
		AResearchStove* Stove = OverlappingResearchStove;
		AddOption(TierWorld, DistTo(Stove), TEXT("清空研究炉"), Stove, [Stove]()
		{
			Stove->ClearFoods();
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow, TEXT("[研究炉] 已清空炉内食材"));
			}
			return true;
		});
	}

	// 灶台/锅：煮好了 → 出锅（切菜生成物需要先放下手上的东西）
	if (MachineCookFinished() && CookingMachine)
	{
		const float MDist = DistTo(CookingMachine);
		if (IsSpawnOther())
		{
			if (NeedDropDown())
			{
				AddOption(TierWorld, MDist, TEXT("放下手持物"), CookingMachine, [this]()
				{
					DoDropDown();
					return true;
				});
			}
			else
			{
				AddOption(TierWorld, MDist, TEXT("取出切好的菜"), CookingMachine, [this]()
				{
					DoSpwanOther();
					return true;
				});
			}
		}
		else
		{
			AddOption(TierWorld, MDist, TEXT("取出成品"), CookingMachine, [this]()
			{
				DoPushDishTest();
				return true;
			});
		}
	}

	// 灶台着火：修理
	if (NeedRepairMachine())
	{
		AddOption(TierWorld, DistTo(CookingMachine), TEXT("修理灶台"), CookingMachine, [this]()
		{
			DoRepair();
			return true;
		});
	}

	// 手上拿着食材：扔进锅
	if (CanThrowToMachine())
	{
		AddOption(TierWorld, DistTo(CookingMachine), TEXT("扔进锅"), CookingMachine, [this]()
		{
			DoThrow();
			return true;
		});
	}

	// 仓库：把背包物品存入（手上物品不受影响；背包为空时无副作用）
	if (OverlappingWarehouse)
	{
		AddOption(TierWorld, DistTo(OverlappingWarehouse), TEXT("存入仓库"), OverlappingWarehouse, [this]()
		{
			OverlappingWarehouse->DepositFromInventory(this);
			return true;
		});
	}

	// 生成箱：取料（手上有东西时按 E 应该是"放下"，不参与取料）
	if (!PickingItem && IsSpawner())
	{
		AddOption(TierWorld, DistTo(OverlappingItem), TEXT("取料"), OverlappingItem, [this]()
		{
			DoSpwan();
			return true;
		});
	}

	// 地面/桌面物品：拾取（手上有东西时同理不参与，让位给 Tier 3 的放下）
	// 刚放下的物品有锁定时间，防连按 E "拾取↔放下"抖动
	if (!PickingItem && CanPick() && !IsDropLockActive(OverlappingItem))
	{
		AddOption(TierWorld, DistTo(OverlappingItem), TEXT("拾取"), OverlappingItem, [this]()
		{
			DoPick();
			return true;
		});
	}

	// ---------------- Tier 3：兜底 —— 放下手上的东西 ----------------
	// 缓冲消费时不执行：玩家在动画期间按的那一下本意是"继续交互"，
	// 补执行时若变成"把刚捡的东西放下"，手感会更差。
	if (NeedDropDown() && !bConsumingBufferedPick)
	{
		AddOption(TierFallback, FLT_MAX, TEXT("放下"), nullptr, [this]()
		{
			DoDropDown();
			return true;
		});
	}
}

void AShootCharacter::RefreshInteractionHighlight()
{
	if (!bEnableInteractionHighlight)
	{
		return;
	}

	TArray<FInteractOption, TInlineAllocator<12>> Options;
	BuildInteractionOptions(Options);

	// 与 Pick() 完全相同的排序规则：先分层、同层按距离，保证"亮着的"就是"按 E 会生效的"
	Options.StableSort(InteractionOptionLess);

	// 只看排在最前面的那一个：它要不是物品/植株（比如灶台、客人、放下），就一个都不亮。
	// 旧实现里每个球各自在 Overlap 时点亮自己，站在一堆食材中间就会亮成一片。
	AActor* NewTarget = Options.Num() > 0 ? Options[0].Target : nullptr;

	if (HighlightTarget == NewTarget)
	{
		return;
	}

	// 熄灭上一个目标
	if (AItemActor* OldItem = Cast<AItemActor>(HighlightTarget))
	{
		if (IsValid(OldItem))
		{
			OldItem->SetInteractionHighlight(false);
		}
	}
	else if (APlant* OldPlant = Cast<APlant>(HighlightTarget))
	{
		if (IsValid(OldPlant))
		{
			OldPlant->SetInteractionHighlight(false);
		}
	}

	HighlightTarget = NewTarget;

	if (InteractionDebug::IsEnabled() && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.2f, FColor::Cyan,
			FString::Printf(TEXT("[高亮] %s"), NewTarget ? *GetNameSafe(NewTarget) : TEXT("无")));
	}

	// 点亮新目标（只有物品和植株支持描边；灶台/客人等目标胜出时全场无高亮）
	if (AItemActor* NewItem = Cast<AItemActor>(NewTarget))
	{
		if (IsValid(NewItem))
		{
			NewItem->SetInteractionHighlight(true);
		}
	}
	else if (APlant* NewPlant = Cast<APlant>(NewTarget))
	{
		if (IsValid(NewPlant))
		{
			NewPlant->SetInteractionHighlight(true);
		}
	}
}

// ============================================================================
// 交互动画期间的输入缓冲
// ============================================================================
bool AShootCharacter::IsInteractionAnimPlaying()
{
	if (!GetMesh())
	{
		return false;
	}
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (!AnimInstance)
	{
		return false;
	}

	// 采集/浇水不改 CharacterActionState（角色一直处于 EAS_Unoccupied 或 EAS_Picking），
	// 所以"动画是否在播"只能看蒙太奇本身
	return (PickMontage && AnimInstance->Montage_IsPlaying(PickMontage))
		|| (GatheringMontage && AnimInstance->Montage_IsPlaying(GatheringMontage))
		|| (WateringMontage && AnimInstance->Montage_IsPlaying(WateringMontage));
}

void AShootCharacter::BufferPickInput()
{
	bHasBufferedPick = true;
	if (GetWorld())
	{
		BufferedPickTime = GetWorld()->GetTimeSeconds();
	}
}

void AShootCharacter::TickBufferedPick(float DeltaTime)
{
	if (!bHasBufferedPick)
	{
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (IsInteractionAnimPlaying())
	{
		// 动画过长或被误配成循环：超过窗口就丢弃，不补执行一个"过期的按键"
		if (Now - BufferedPickTime > PickBufferWindow)
		{
			bHasBufferedPick = false;
		}
		return;
	}

	bHasBufferedPick = false;
	bConsumingBufferedPick = true;
	Pick(FInputActionValue());
	bConsumingBufferedPick = false;
}

void AShootCharacter::PickReleased(const FInputActionValue& Value)
{
	bPickHeld = false;
	PickHeldTime = 0.f;
	RangePickCooldown = 0.f;
}

// ============================================================================
// 按住 E 范围拾取（野外收割）：长按超过 RangePickHoldTime 后，
// 每隔 RangePickInterval 把半径内最近的散落物品直接收进背包（不占手）
// ============================================================================
void AShootCharacter::TickRangePick(float DeltaTime)
{
	// 手上拿着脏碟时不收割：脏碟状态不允许做别的动作（原逻辑就这么约束的）
	if (!bRangePickEnabled || !bPickHeld || IsInteractionAnimPlaying()
		|| (PickingItem && PickingItem->bIsDirtyPlate))
	{
		PickHeldTime = 0.f;
		return;
	}

	PickHeldTime += DeltaTime;
	if (PickHeldTime < RangePickHoldTime)
	{
		return; // 还没到长按判定：短按仍然只做一次普通交互
	}

	RangePickCooldown -= DeltaTime;
	if (RangePickCooldown > 0.f)
	{
		return;
	}

	// 贴着设备时不吸：否则站在灶台/上菜台旁长按 E 会把待加工食材吸进背包
	if (HasBlockingInteractionTarget())
	{
		return;
	}

	if (AItemActor* Item = FindNearestRangePickItem())
	{
		RangePickCooldown = RangePickInterval;
		const FString ItemName = Item->GetName();
		if (AddItemToInventory(Item)) // 内部销毁世界物体，之后不能再访问 Item
		{
			RemoveOverlappingItem(Item);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 1.f, FColor::Green,
					FString::Printf(TEXT("[范围拾取] %s 已入背包"), *ItemName));
			}
		}
	}
}

AItemActor* AShootCharacter::FindNearestRangePickItem() const
{
	if (!GetWorld())
	{
		return nullptr;
	}

	AItemActor* Best = nullptr;
	float BestDistSq = FMath::Square(RangePickRadius);
	const FVector MyLoc = GetActorLocation();

	for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
	{
		AItemActor* Item = *It;
		if (!Item || !IsValid(Item) || Item == PickingItem)
		{
			continue;
		}
		// 空碟/脏碟走各自的交互流程，不能当散落物吸走
		if (!Item->bPickable || Item->bIsEmptyPlate || Item->bIsDirtyPlate)
		{
			continue;
		}

		const EItemState State = Item->GetItemState();
		if (State == EItemState::EIS_Equipped || State == EItemState::EIS_Cooking)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(MyLoc, Item->GetActorLocation());
		if (DistSq > BestDistSq)
		{
			continue;
		}
		// 隔着墙/台面的不吸
		if (!HasInteractionLineOfSight(Item))
		{
			continue;
		}

		BestDistSq = DistSq;
		Best = Item;
	}

	return Best;
}

bool AShootCharacter::HasBlockingInteractionTarget() const
{
	// 只列"设备/服务类"：这些位置长按 E 的意图一定是操作设备，不是收割地面散落物
	return CookingMachine != nullptr
		|| OverlappingCounter != nullptr
		|| OverlappingAttractCounter != nullptr
		|| OverlappingGate != nullptr
		|| OverlappingGuest != nullptr
		|| OverlappingChairSeat != nullptr
		|| OverlappingDishwasher != nullptr
		|| OverlappingResearchStove != nullptr
		|| OverlappingWarehouse != nullptr;
}

 void AShootCharacter::PushFoodStart(const FInputActionValue& Value)
 {
 	if (NeedDropDown())
 	{
 		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("PushFoodStart"));
 		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("P键按下 - 开始绘制抛物线"));
 		bIsPKeyHeld = true;
 		CurrentAngle = 0.0f;
 	}
 }



 void AShootCharacter::PushFoodComplete(const FInputActionValue& Value)
 {
 	if (NeedDropDown())
 	{
 		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("P键松开 - 停止绘制抛物线"));
 		OnPushFoodComplete();
 		ClearPrediction();
 		bIsPKeyHeld = false;
 		CurrentAngle = 0.0f;
 	
 	}
 }

 void AShootCharacter::OnMouseWheelAxis(const FInputActionValue& Value)
 {
 	float WheelFloat = Value.Get<float>();

 	//CurrentAngle = (WheelFloat/10.0f) + CurrentAngle;

 	CurrentAngle+=WheelFloat*10.0f;
 	CurrentAngle = FMath::Clamp(CurrentAngle, MinVerticalAngle, MaxVerticalAngle);
 }


 void AShootCharacter::SetWeaponCollisionEnabled(ECollisionEnabled::Type CollisionEnabled)
{
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("SetWeaponCollisionEnabled"));
 	//if (EquippedWeapon == nullptr)
 	//{
 	//	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("SetWeaponCollisionEnabled nil"));
 	//}
	if (EquippedWeapon && EquippedWeapon->GetWeaponBox())
	{
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("SetWeaponCollisionEnabled: %d"),CollisionEnabled));
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("SetWeaponCollisionEnabled Come"));
		EquippedWeapon->GetWeaponBox()->SetCollisionEnabled(CollisionEnabled);
		EquippedWeapon->IgnoreActors.Empty();
	}
}

void AShootCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AShootCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// 仅保留左右旋转，禁止上下俯仰
		AddControllerYawInput(Yaw);
		if (bRotationPicth)
		{
			AddControllerPitchInput(Pitch);
		}
		
	}
}

void AShootCharacter::DoJumpStart()
{
	Jump();
}

void AShootCharacter::DoJumpEnd()
{
	StopJumping();
}



void AShootCharacter::DoAttack()
{
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("DoAttack"));
 	AWeapon* Weapon = GetEqiuppedWeapon();
 	if (Weapon != nullptr)
 	{
 		Weapon->AttachMeshToSocket(RetargetMesh, FName("hand_rSocket"));
 	}
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance && AttackMontage)
	{
		AnimInstance->Montage_Play(AttackMontage);
	}
	CharacterActionState = ECharacterActionState::EAS_Attacking;

}

void AShootCharacter::DoPick()
{
 	if (OverlappingItem == nullptr)
 	{
 		return;
 	}

	// 打烊（未营业）期间：按全局开关把“拾取”重定向为“直接收进背包”，玩家手上不残留物品；
	// 营业时仍走原逻辑（拿在手上）。取不到时钟时按“营业中”处理，不特判。
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		const bool bBusinessOpen = (GI->businessSimulation && GI->businessSimulation->TimeSystem)
			? GI->businessSimulation->TimeSystem->IsBusinessOpen()
			: true;
		if (!bBusinessOpen && GI->bAutoStoreToBackpackWhileClosed)
		{
			AddItemToInventory(OverlappingItem); // 内部销毁世界物体并记入背包
			PlayGatheringMontage(); // 收进背包时播放采集动画（此时动作状态为空闲，可正常播放）
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green,
					TEXT("[拾取] 打烊期间已自动收进背包"));
			}
			SetOverlappingItem(nullptr);
			return;
		}
	}

 	IsPicking = true;
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("DoPick"));
 	if(OverlappingItem->GetItemState() != EItemState::EIS_Cooking && OverlappingItem->GetItemState() != EItemState::EIS_Equipped) //手上没有食物
	{
		// if (OverlappingItem->ItemType == EItemType::EIT_Spawn && OverlappingItem->SpawnItemActorClass)//如果是需要生成食物的类型，如番茄树，要从番茄树生成一个番茄到手上
		// {
		// 	//这一个放在他的蓝图里处理
		// 	OverlappingItem->OnSpawnActorEvent();
		// 	PlayPickMontage( FName("Pick0")); //pick1 是摘，摘完后应该切到抬
		// 	//原本在c++处理
		// 	//UWorld* World = GetWorld();
		// 	//AItemActor* Spawned = World->SpawnActor<AItemActor>(OverlappingItem->SpawnItemActorClass, GetActorLocation(), GetActorRotation());
		// 	//SetOverlappingItem(Spawned);
		// 	//SetPickingItemStatus();
		// }
		// else
		{
			//如果不是番茄树的情况
			SetPickingItemStatus();//这一个放在他的蓝图里处理
			PlayPickMontage( FName("Pick0"));
		}
	}

 	IsPicking = false;
 	//拿到手上后，把Hovering item设置为空，并从候选集移除（物品已 Equipped 挂到手上）
	RemoveOverlappingItem(OverlappingItem);
 	//SetWeaponCollisionEnabled(ECollisionEnabled::NoCollision);

}

 void AShootCharacter::DoSpwan()
 {
 	if (OverlappingItem == nullptr)
 	{
 		return;
 	}
 	//IsPicking = true;
 	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("DoPick"));
 	if(OverlappingItem->GetItemState() != EItemState::EIS_Cooking && OverlappingItem->GetItemState() != EItemState::EIS_Equipped) //手上没有食物
 	{
 		if (AFoodSpawner* FoodSpawner = Cast<AFoodSpawner>(OverlappingItem))//如果是需要生成食物的类型，如番茄树，要从箱子生成一个番茄到手上
		{
			//箱内还有食材才生成；为空(RemainingCount==0)则不再生成
			if (FoodSpawner->CanSpawn() && FoodSpawner->SpawnItemActorClass)
			{
				//原本在c++处理
				UWorld* World = GetWorld();
				AItemActor* Spawned = World->SpawnActor<AItemActor>(FoodSpawner->SpawnItemActorClass, GetActorLocation(), GetActorRotation());
				SetOverlappingItem(Spawned);
				SetPickingItemStatus();
				FoodSpawner->OnSpawnActorEvent();//广播给蓝图（特效/音效等）
				FoodSpawner->ConsumeOne();//消耗一个数量（空了就不会再生成）
			}
		}
 	}

 	//IsPicking = false;
 	//生成物已拿在手上：从候选集重选目标（食物箱仍在脚下时保持为当前目标，HUD 数量同步刷新）
 	RefreshBestOverlappingItem();
 	//SetWeaponCollisionEnabled(ECollisionEnabled::NoCollision);
 }

 void AShootCharacter::DoSpwanOther()
 {
 	CookingMachine->GetMenuFood();
 	for (int32 i = 0; i < CookingMachine->FoodActors.Num(); i++)
 	{
		AItemActor* CurrentActor = CookingMachine->FoodActors[i];
		if (AFoodSpawnOther* SpawnOther = Cast<AFoodSpawnOther>(CurrentActor))//如果是需要生成食物的类型，如牛排，要从牛排变成切好的牛排
		{
			if (SpawnOther->SpawnItemActorClass)
			{
				UWorld* World = GetWorld();
				AItemActor* Spawned = World->SpawnActor<AItemActor>(SpawnOther->SpawnItemActorClass, GetActorLocation(), GetActorRotation());
				SetOverlappingItem(Spawned);
				SetPickingItemStatus();
			}
		}
		CurrentActor->Destroy();
	}
 	CookingMachine->FoodActors.Empty();
 	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("切好菜"));
 	RefreshBestOverlappingItem();
 }

 void AShootCharacter::DoDropDown() // 如果已经手上有食物了，要先将食物扔下
 {
 	PickingItem->DeAttachMeshToSocket();
 	//PickingItem->SetItemState(EItemState::EIS_Idle);//临时简单处理，为了可以放下食物可以马上拾取

 	OnReleasePickEvent();
 	PickingItem->SetItemState(EItemState::EIS_Hovering);//临时简单处理，为了可以放下食物可以马上拾取
 	//放下的物品重新进入候选集（其交互球仍与角色重叠，不会再触发 BeginOverlap，需手动注册）
 	LastDroppedItem = PickingItem; // 记录重拾锁定（DropRePickLockTime 秒内不可再拾）
 	LastDropTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
 	AddOverlappingItem(PickingItem);
 	SetPickingItem(nullptr);
 	CharacterActionState = ECharacterActionState::EAS_Unoccupied;
 	CharacterState = ECharacterState::ECS_Idle;

 	//设置ik
 	
 	FBIK_Alpha = 0;
 	HandIK_LPoint = nullptr;
 	HandIK_RPoint = nullptr;
 }

 void AShootCharacter::OnPushFoodComplete()
 {
	if (!PickingItem)
	{
		return;
	}

	// 抛出去的速度要在脱离挂点之前算：GetLaunchVelocity 依赖当前手部朝向/位置
	const FVector ForwardDirection = GetLaunchVelocity();

	// 先脱离挂点，再打开"物理 + 世界碰撞"。
	// 顺序不能反：挂在手上时开物理会带着挂点的相对变换被弹飞。
	PickingItem->DeAttachMeshToSocket();
	PickingItem->EnablePhysicsDrop();
	PickingItem->ItemMesh->SetPhysicsLinearVelocity(ForwardDirection);

 	OnReleasePickEvent();
 	PickingItem->SetItemState(EItemState::EIS_Idle);
 	SetPickingItem(nullptr);
 	CharacterActionState = ECharacterActionState::EAS_Unoccupied;
 	CharacterState = ECharacterState::ECS_Idle;

 	//设置ik
 	
 	FBIK_Alpha = 0;
 	HandIK_LPoint = nullptr;
 	HandIK_RPoint = nullptr;
 }

 void AShootCharacter::SetPickingItemStatus()
{
 	AActor* PreAttachActor = OverlappingItem->GetAttachParentActor();
 	if (PreAttachActor != nullptr)
 	{
 		OverlappingItem->PreAttachActor = PreAttachActor; //FChangeAttachActorEvent
 		OverlappingItem->OnChangeAttachActorEvent.Broadcast();//广播出去，切换了
 	}
	//OverlappingItem->AttachMeshToSocket(GetMesh(), ItemSocketName);
	OverlappingItem->AttachMeshToSocket(RetargetMesh, ItemSocketName);

	OverlappingItem->SetItemState(EItemState::EIS_Equipped);//这里经常闪退
	SetPickingItem(OverlappingItem);
	OnPickEvent();
	CharacterActionState = ECharacterActionState::EAS_Picking;
	CharacterState = ECharacterState::ECS_Holding;

	//设置ik
	if (OverlappingItem->HandIK_LPoint != nullptr)
	{
		FBIK_Alpha = 1;
		HandIK_LPoint = OverlappingItem->HandIK_LPoint;
		HandIK_RPoint = OverlappingItem->HandIK_RPoint;
	}
}

void AShootCharacter::HoldItemInHand(AItemActor* Item, bool bSetAsPickingItem)
{
	if (!Item)
	{
		return;
	}

	// 从树/插槽上摘下，直接挂到角色手部插槽上
	Item->DeAttachMeshToSocket();
	Item->AttachMeshToSocket(RetargetMesh, ItemSocketName);
	Item->SetItemState(EItemState::EIS_Equipped);

	// 设为手持主物体并切换到"手持"状态（仅主物体需要，多个果实时其它只挂手上）
	if (bSetAsPickingItem)
	{
		SetPickingItem(Item);
		OnPickEvent();
		CharacterActionState = ECharacterActionState::EAS_Picking;
		CharacterState = ECharacterState::ECS_Holding;

		if (Item->HandIK_LPoint != nullptr)
		{
			FBIK_Alpha = 1;
			HandIK_LPoint = Item->HandIK_LPoint;
			HandIK_RPoint = Item->HandIK_RPoint;
		}
	}
}

// ============ 背包系统 ============
bool AShootCharacter::AddItemToInventory(AItemActor* Item)
{
	if (!Item)
	{
		return false;
	}
	if (IsInventoryFull())
	{
		UE_LOG(LogShoot, Warning, TEXT("[%s] 背包已满，无法放入 %s"),
			*GetName(), *Item->GetName());
		return false;
	}

	// 同类物品堆叠：已存在相同 ItemClass 的槽位则数量+1，不再新增槽位
	UClass* InClass = Item->GetClass();
	for (FInventorySlot& Existing : InventoryItems)
	{
		if (Existing.ItemClass == InClass)
		{
			Existing.Quantity++;
			Item->Destroy(); // 入包即删除世界中的物体
			OnInventoryChanged.Broadcast();
			UE_LOG(LogShoot, Log, TEXT("[%s] 物品 %s 已堆叠，当前数量 %d(槽位数 %d)"),
				*GetName(), *Existing.DisplayName, Existing.Quantity, InventoryItems.Num());
			return true;
		}
	}

	// 记录物体的"类型/信息"，随后销毁世界中的物体(背包只保留轻量记录)
	FInventorySlot Slot;
	Slot.ItemClass = InClass;            // 取出时按此类重新生成(保留蓝图子类)；类型已由 ItemClass 体现，无需单独枚举
	Slot.ItemInfoRow = Item->ItemInfoRow;
	Slot.FoodIdArray = Item->FoodIdArray;
	// 显示名优先取自数据表行的 ItemDes
	if (const FItemActorInfo* InfoRow = Item->ItemInfoRow.GetRow<FItemActorInfo>(TEXT("ItemInfo")))
	{
		Slot.DisplayName = InfoRow->ItemDes;
	}
	Slot.Quantity = 1;
	InventoryItems.Add(Slot);

	Item->Destroy(); // 入包即删除世界中的物体

	OnInventoryChanged.Broadcast();
	UE_LOG(LogShoot, Log, TEXT("[%s] 物品 %s 已放入背包(已销毁世界实例)，当前数量 %d"),
		*GetName(), *Slot.DisplayName, InventoryItems.Num());
	return true;
}

bool AShootCharacter::AddSlotToInventory(const FInventorySlot& Slot)
{
	if (IsInventoryFull())
	{
		UE_LOG(LogShoot, Warning, TEXT("[%s] 背包已满，无法放入 %s"),
			*GetName(), *Slot.DisplayName);
		return false;
	}

	// 同类物品堆叠：已存在相同 ItemClass 的槽位则数量+1，不再新增槽位
	for (FInventorySlot& Existing : InventoryItems)
	{
		if (Existing.ItemClass == Slot.ItemClass)
		{
			Existing.Quantity += Slot.Quantity;
			OnInventoryChanged.Broadcast();
			UE_LOG(LogShoot, Log, TEXT("[%s] 物品 %s 已堆叠回背包，当前数量 %d(槽位数 %d)"),
				*GetName(), *Slot.DisplayName, Existing.Quantity, InventoryItems.Num());
			return true;
		}
	}

	InventoryItems.Add(Slot);
	OnInventoryChanged.Broadcast();
	UE_LOG(LogShoot, Log, TEXT("[%s] 物品 %s 已放回背包，当前槽位数 %d"),
		*GetName(), *Slot.DisplayName, InventoryItems.Num());
	return true;
}

bool AShootCharacter::RemoveItemFromInventory(int32 SlotIndex)
{
	if (!InventoryItems.IsValidIndex(SlotIndex))
	{
		return false;
	}
	InventoryItems.RemoveAt(SlotIndex);
	OnInventoryChanged.Broadcast();
	return true;
}

bool AShootCharacter::HasItemInInventory(int32 SlotIndex) const
{
	return InventoryItems.IsValidIndex(SlotIndex);
}

bool AShootCharacter::IsInventoryFull() const
{
	return InventoryCapacity > 0 && InventoryItems.Num() >= InventoryCapacity;
}

void AShootCharacter::StoreHeldItemToInventory()
{
	if (!PickingItem)
	{
		return;
	}

	// 先解除手持状态(从手上/插槽分离)，再记录进背包(AddItemToInventory 内部会销毁物体)
	// 注意：先保存局部指针，因为 SetPickingItem(nullptr) 会把成员 PickingItem 置空，
	// 否则下面 AddItemToInventory(PickingItem) 传进去的是 null，导致物体不会被销毁。
	AItemActor* ItemToStore = PickingItem;
	ItemToStore->DeAttachMeshToSocket();
	OnReleasePickEvent();
	SetPickingItem(nullptr);
	CharacterActionState = ECharacterActionState::EAS_Unoccupied;
	CharacterState = ECharacterState::ECS_Idle;
	FBIK_Alpha = 0;
	HandIK_LPoint = nullptr;
	HandIK_RPoint = nullptr;

	PlayGatheringMontage(); // 收进背包时播放采集动画（上方已复位为 EAS_Unoccupied，可正常播放）
	AddItemToInventory(ItemToStore); // ItemToStore 在内部被销毁，注意之后不要再访问它
}

AItemActor* AShootCharacter::TakeOutInventoryItem(int32 SlotIndex)
{
	if (!InventoryItems.IsValidIndex(SlotIndex))
	{
		return nullptr;
	}
	FInventorySlot& Slot = InventoryItems[SlotIndex];
	if (!Slot.ItemClass)
	{
		return nullptr;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// 手上已有物体则先放下，避免冲突
	if (PickingItem)
	{
		DoDropDown();
	}

	// 按记录的类型在角色位置生成新物体
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AItemActor* Spawned = World->SpawnActor<AItemActor>(Slot.ItemClass, GetActorLocation(), GetActorRotation(), Params);
	if (!Spawned)
	{
		return nullptr;
	}

	// 回填记录的信息
	Spawned->ItemInfoRow = Slot.ItemInfoRow;
	Spawned->FoodIdArray = Slot.FoodIdArray;

	// 装备到手上(复用拾取逻辑：挂到 ItemSocketName + 设为 PickingItem)
	Spawned->AttachMeshToSocket(RetargetMesh, ItemSocketName);
	Spawned->SetItemState(EItemState::EIS_Equipped);
	SetPickingItem(Spawned);
	OnPickEvent();
	CharacterActionState = ECharacterActionState::EAS_Picking;
	CharacterState = ECharacterState::ECS_Holding;
	if (Spawned->HandIK_LPoint != nullptr)
	{
		FBIK_Alpha = 1;
		HandIK_LPoint = Spawned->HandIK_LPoint;
		HandIK_RPoint = Spawned->HandIK_RPoint;
	}

	// 取出一件：数量-1；归零才移除该槽位(同类堆叠只减数量不删槽)
	Slot.Quantity--;
	if (Slot.Quantity <= 0)
	{
		InventoryItems.RemoveAt(SlotIndex);
	}
	OnInventoryChanged.Broadcast();
	UE_LOG(LogShoot, Log, TEXT("[%s] 从背包取出 %s，挂到手上(剩余数量 %d)"),
		*GetName(), *Slot.DisplayName, FMath::Max(Slot.Quantity, 0));
	return Spawned;
}

void AShootCharacter::TakeOutFirstInventoryItem()
{
	if (InventoryItems.Num() == 0)
	{
		return;
	}
	TakeOutInventoryItem(0);
}

void AShootCharacter::ToggleInventory()
{
	if (!InventoryWidgetClass)
	{
		UE_LOG(LogShoot, Warning, TEXT("[%s] 未配置 InventoryWidgetClass，无法打开背包"), *GetName());
		return;
	}

	// 已打开则关闭
	if (InventoryWidget && InventoryWidget->IsInViewport())
	{
		InventoryWidget->RemoveFromParent();
		return;
	}

	// 首次打开：创建并绑定刷新(只创建一次，关闭后复用)
	if (!InventoryWidget)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (PC)
		{
			InventoryWidget = CreateWidget<UInventoryWidget>(PC, InventoryWidgetClass);
		}
		else
		{
			InventoryWidget = CreateWidget<UInventoryWidget>(GetWorld(), InventoryWidgetClass);
		}
		if (InventoryWidget)
		{
			InventoryWidget->OwnerCharacter = this;
			// 关键：设为不可聚焦，否则 Widget 会抢占键盘焦点，导致再次按 B 时
			// 按键被 Slate 吞掉、EnhancedInput 收不到，背包无法关闭。
			InventoryWidget->SetIsFocusable(false);
			// 背包变化(增删)时自动刷新界面，只绑一次
			OnInventoryChanged.AddDynamic(InventoryWidget, &UInventoryWidget::RefreshInventory);
		}
	}

	if (InventoryWidget)
	{
		InventoryWidget->AddToViewport(10);
		InventoryWidget->RefreshInventory();
	}
}

void AShootCharacter::ToggleShop()
{
	if (!ShopWidgetClass)
	{
		UE_LOG(LogShoot, Warning, TEXT("[%s] 未配置 ShopWidgetClass，无法打开商店"), *GetName());
		return;
	}

	// 已打开则关闭
	if (ShopWidget && ShopWidget->IsInViewport())
	{
		ShopWidget->RemoveFromParent();
		return;
	}

	// 首次打开：创建并绑定经济系统(只创建一次，关闭后复用)
	if (!ShopWidget)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (PC)
		{
			ShopWidget = CreateWidget<UShopWidget>(PC, ShopWidgetClass);
		}
		else
		{
			ShopWidget = CreateWidget<UShopWidget>(GetWorld(), ShopWidgetClass);
		}
		if (ShopWidget)
		{
			ShopWidget->OwnerCharacter = this;
			// 设为不可聚焦，避免抢占键盘焦点导致再次按 P 失灵
			ShopWidget->SetIsFocusable(false);
			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
			{
				ShopWidget->BindEconomy(GI->GetEconomySystem());
			}
		}
	}

	if (ShopWidget)
	{
		ShopWidget->AddToViewport(20);
		ShopWidget->RefreshShop();
	}
}



void AShootCharacter::DoThrow()
{
	if (PickingItem)
	{
		//1.播放扔的动画
		UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
		if (AnimInstance && ThrowMontage)
		{
			AnimInstance->Montage_Play(ThrowMontage);
		}
		
		
		if (CookingMachine)
		{
			// 食材 ID 以数据表行名为准（与 AItemActor::GetItemID 同一套解析）：行名为纯数字即 ItemID，
			// 行内 ItemID 字段只做兜底。直接读字段时，行内未填（默认 -1）会永远匹配不到菜品
			const int32 FoodMaterialID = PickingItem->GetItemID();
			if (CookingMachine->AddFoodMateral(FoodMaterialID))
			{
				PickingItem->DeAttachMeshToSocket();
				CookingMachine->FoodActors.Add(PickingItem);
				//设置到锅上的Socket,
				PickingItem->AttachMeshToSocket(CookingMachine->ItemMesh, FName("PointSocket"));
				
			//	FVector SocketWorldLocation = CookingMachine->ItemMesh->GetSocketLocation( FName("PointSocket"));
		//		PickingItem->SetActorLocation(SocketWorldLocation);
				
				PickingItem->SetItemState(EItemState::EIS_Cooking);
				OnThrowSucessEvent();	
				//设置状态在Montage蓝图设置
				CharacterActionState = ECharacterActionState::EAS_Unoccupied;
				CharacterState = ECharacterState::ECS_Idle;
				SetPickingItem(nullptr);
				//设置ik
				FBIK_Alpha = 0;
				HandIK_LPoint = nullptr;
				HandIK_RPoint = nullptr;
				//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("CookingMachine？？？"));
			}
			else
			{
				// 失败诊断：区分"物品 ID 无效"与"菜品/机器配置不匹配"，只进 Output Log，不刷屏幕
				const UDataTable* ItemTable = PickingItem->ItemInfoRow.DataTable;
				UE_LOG(LogTemp, Warning,
					TEXT("[CookingMachine] 食材 ItemID=%d 无法下锅（%s，本机 MachineID=%d）；物品数据表=%s，行名=%s：请检查 DT_FoodID 行名/ItemID、菜品 MenuItems 是否含该食材、菜品 MachineID 是否与本机匹配（0=通用），以及锅是否着火/维修中"),
					FoodMaterialID, *CookingMachine->GetName(), CookingMachine->MachineID,
					ItemTable ? *ItemTable->GetName() : TEXT("NULL"),
					*PickingItem->ItemInfoRow.RowName.ToString());
			}

		}

		//当机器完成后，进行推菜测试，就是cookingTime =0的时候，直接上菜
		if (MachineCookFinished())
		{
			DoPushDishTest();
		}
	}
}

 void AShootCharacter::DoRepair()
 {
	CharacterState = ECharacterState::ECS_Repairing;
	CookingMachine->Repairing = true;
	//修理的时候，人不可以动
	GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_None);
 	AWeapon* Weapon = GetEqiuppedWeapon();
 	if (Weapon != nullptr)
 	{
 		Weapon->AttachMeshToSocket(RetargetMesh, FName("hand_rSocket"));
 	}

 	//让人物朝向锅
 	// 计算看向目标的旋转
 	FVector CharacterLocation = GetActorLocation();
 	FVector TargetLocation = CookingMachine->GetActorLocation();
 	FRotator TargetRotation = (TargetLocation - CharacterLocation).Rotation();
    
 	// 设置角色旋转（只设置Yaw，保持角色直立）
 	FRotator NewRotation = FRotator(0, TargetRotation.Yaw, 0);
 	SetActorRotation(NewRotation);
 }

void AShootCharacter::DoRepairEnd()
{
	CharacterState = ECharacterState::ECS_Idle;
	CookingMachine->Repairing = false;
 	//修理完把食物删除掉
 	for (int32 i = 0; i < CookingMachine->FoodActors.Num(); i++)
 	{
 		AItemActor* CurrentActor = CookingMachine->FoodActors[i];
 		CurrentActor->Destroy();
 	}
 	CookingMachine->FoodActors.Empty();
 	AWeapon* Weapon = GetEqiuppedWeapon();
 	if (Weapon != nullptr)
 	{
 		//Weapon->AttachMeshToSocket(GetMesh(), FName("spine_02Socket"));
 		Weapon->AttachMeshToSocket(RetargetMesh, FName("spine_02Socket"));
 		
 	}
	GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_Walking);
}

 void AShootCharacter::DoPutDish()
 {
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("菜煮好了，放碟子上"));
 	int FoodId = CookingMachine->GetMenuFood();
	if (!PickingItem)
	{
		return;
	}
	// 生成位置给盘子的世界位置：挂点（PointSocket）在资产里没配时，
	// 成品也会落在盘子上，而不是停在世界原点掉到地板下面。
	const FVector SpawnLocation = PickingItem->ItemMesh
		? PickingItem->ItemMesh->GetComponentLocation()
		: PickingItem->GetActorLocation();
	auto FoodActor = Cast<UBaseGameInstance>(GetGameInstance())->foodSystem->CreateFoodActor(FoodId, SpawnLocation);
 	{
 		FoodActor->AttachMeshToSocket(PickingItem->ItemMesh, FName("PointSocket"));
 		PickingItem->FoodIdArray.Add(FoodId) ;
 		PickingItem->FoodArray.Add(FoodActor);
 		for (int32 i = 0; i < CookingMachine->FoodActors.Num(); i++)
 		{
 			AItemActor* CurrentActor = CookingMachine->FoodActors[i];
 			CurrentActor->Destroy();
 		}
 		CookingMachine->FoodActors.Empty();
 	}
 }

 void AShootCharacter::DoGiveDishToGuest()
 {
 	bool success = Cast<UBaseGameInstance>(GetGameInstance())->curLevelBP->orderSystem->PushFoods(PickingItem->FoodIdArray);
 	if (success)
 	{
 		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("成功上菜给客人"));
		OverlappingItem->OnGiveEffectEvent();//上菜的时候显示特效
		if (ADish* Dish = Cast<ADish>(PickingItem))//移除一个碟子（碟子类持有 OnRemoveActorEvent）
		{
			Dish->OnRemoveActorEvent();
		}
		PickingItem->OnClear();
 		PickingItem->Destroy();
 		PickingItem = nullptr;
 		CharacterState = ECharacterState::ECS_Idle;
 		//移除一个碟子
 		//Cast<UBaseGameInstance>(GetGameInstance())->curLevelBP->PlateSystem->RemovePlate();
 	}
 	else
 	{
 	//	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("失败: %d"),PickingItem->FoodIdArray[0]));
 	}
 }

 void AShootCharacter::DoPushDishTest() //直接原地上菜快速测试
 {
 	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("直接原地上菜快速测试---1"));
 	int FoodId = CookingMachine->TargetMenuData.FoodID;
 	//PickingItem->FoodIdArray.Add(FoodId) ;
 	TArray<int> FoodIdArray ;
 	FoodIdArray.Add(FoodId);

 	bool success = FoodId != -1 && Cast<UBaseGameInstance>(GetGameInstance())->curLevelBP->orderSystem->PushFoods(FoodIdArray);
 	if (success)
 	{
 		CookingMachine->GetMenuFood();
 		for (int32 i = 0; i < CookingMachine->FoodActors.Num(); i++)
 		{
		    AItemActor* CurrentActor = CookingMachine->FoodActors[i];
		    CurrentActor->Destroy();
 		}
 		CookingMachine->FoodActors.Empty();
 		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("成功上菜给客人"));
 	//	OverlappingItem->OnGiveEffectEvent();//上菜的时候显示特效
 	//	PickingItem->OnRemoveActorEvent();//移除一个碟子,放在蓝图
 	//	PickingItem->OnClear();
 	//	PickingItem->Destroy();
 	//	PickingItem = nullptr;
 		CharacterState = ECharacterState::ECS_Idle;
 		//移除一个碟子
 		//Cast<UBaseGameInstance>(GetGameInstance())->curLevelBP->PlateSystem->RemovePlate();
 	}
    else
    {
    	
    	GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red
			, FString::Printf(TEXT("上菜失败%i"), FoodId));
    }
 }

 void AShootCharacter::TriggerDeadFunc(int32 RebirthTime)
 {
 	CharacterState = ECharacterState::ECS_Dead;
 	CountdownTime  = RebirthTime;
 	if (FreezeInput)
 	{
 		if (APlayerController* APC = Cast<APlayerController>(GetController()))
 		{
 			APC->SetIgnoreMoveInput(true);
 		}
 		
 	}
 	

 	// 若未通过文件夹加载，尝试直接加载路径
 	if (!BaseGrayscaleMaterial)
 	{
 		BaseGrayscaleMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Demo/Material/PP_Grey/M_PP_Grey"));
 	}
 	if (BaseGrayscaleMaterial&& !GrayscaleMID)
 	{
 		GrayscaleMID = UMaterialInstanceDynamic::Create(BaseGrayscaleMaterial, this);
 	}
 	if (GrayscaleMID)
 	{
 		GetFollowCamera()->PostProcessSettings.AddBlendable(GrayscaleMID, 1.0f);
 	}

 	
 	// 开始倒计时
 	// 设置计时器：每1秒执行一次UpdateCountdown，共执行3次
 	GetWorldTimerManager().SetTimer(
		 CountdownTimerHandle,
		 this,
		 &AShootCharacter::UpdateCountdown,
		 1.0f,      // 间隔1秒
		 true,      // 循环
		 0.0f       // 初始延迟0秒
	 );
 }


 void AShootCharacter::Tick(float DeltaTime)
 {
	Super::Tick(DeltaTime);

	if (CharacterState == ECharacterState::ECS_Dead)
	{
		
	}
 	
	if (IsRepairing())
	{
		if (CookingMachine->OnFire == false)
		{
			DoRepairEnd();
		}
	}

 	if (CacheAnimInstance == nullptr)
 	{
 		UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
 		UShootAnimInstance* ani = Cast<UShootAnimInstance>(AnimInstance);
 		CacheAnimInstance = ani;
 	}
	if (CacheAnimInstance != nullptr)
	{
		CacheAnimInstance->FBIK_Alpha = FBIK_Alpha;
		if (FBIK_Alpha == 1)
		{
			if (HandIK_LPoint)
			{
				auto tf = HandIK_LPoint->GetComponentTransform();
				//转坐标
				CacheAnimInstance->HandIK_L = HandIK_LPoint->GetComponentTransform();
			}
			if (HandIK_RPoint)
			{
				CacheAnimInstance->HandIK_R = HandIK_RPoint->GetComponentTransform();
			}
		}
	}

 	if (bIsPKeyHeld)
 	{
 		if (bDrawLines)
 		{
 			UpdateCharacterRotation(DeltaTime);
 		}
 		PredictAndDrawParabola();
 	}

	// 采集/浇水时的平滑转身：插值逼近目标朝向，到位后停止（取代原来的瞬时 SetActorRotation）
	if (bHasInteractFaceRotation)
	{
		const FRotator CurrentRot = GetActorRotation();
		if (CurrentRot.Equals(InteractFaceRotation, 1.f))
		{
			bHasInteractFaceRotation = false;
		}
		else
		{
			SetActorRotation(FMath::RInterpTo(CurrentRot, InteractFaceRotation, DeltaTime, 12.f));
		}
	}

	// 冲刺 FOV 过渡（默认关闭）：FOV 变化会放大画面边缘的位移，配合加速更容易产生前冲感。
	// 需要速度感时在蓝图打开 bUseSprintFOV，幅度保持 ≤2 度。
	if (bUseSprintFOV && FollowCamera && !FMath::IsNearlyEqual(FollowCamera->FieldOfView, TargetFOV, 0.05f))
	{
		FollowCamera->SetFieldOfView(FMath::FInterpTo(FollowCamera->FieldOfView, TargetFOV, DeltaTime, FOVInterpSpeed));
	}

	// 注意：这里刻意不再按冲刺状态改 CameraLagSpeed。
	// LagSpeed 一变，相机滞后的距离就跟着跳变，画面会“突”一下往前冲。
	// 现在全程固定 CameraLagSpeed，并靠 CameraLagMaxDistance 封顶滞后幅度。

	// ============ 交互目标定期重估 ============
	// 只靠 Overlap 事件的话，玩家绕到墙后 / 物体被搬走 / 两个范围交叠时目标会残留或选错。
	// 每 InteractionRefreshInterval 秒统一重算一次，代价可以忽略。
	InteractionRefreshTimer -= DeltaTime;
	if (InteractionRefreshTimer <= 0.f)
	{
		InteractionRefreshTimer = InteractionRefreshInterval;
		RefreshAllInteractionTargets();
	}

	// ============ 输入缓冲 / 按住 E 范围拾取 ============
	// 缓冲：交互动画期间按下的 E 在动画结束后补执行一次
	// 范围拾取：按住 E 超过阈值后，持续把身边散落物收进背包
	TickBufferedPick(DeltaTime);
	TickRangePick(DeltaTime);

	// ============ 交互视线调试（控制台 Shoot.DebugInteract 1） ============
	if (InteractionDebug::IsEnabled() && GetWorld())
	{
		// 把所有"当前选中的交互目标"连成绿线（视线通）/ 红线（被遮挡），
		// 一眼就能看出按 E 会作用到哪个东西、以及是不是被墙挡住
		TArray<AActor*, TInlineAllocator<10>> DebugTargets;
		auto AddDebugTarget = [&DebugTargets](AActor* Target)
		{
			if (IsValid(Target))
			{
				DebugTargets.AddUnique(Target);
			}
		};
		AddDebugTarget(OverlappingItem);
		AddDebugTarget(OverlappingPlant);
		AddDebugTarget(CookingMachine);
		AddDebugTarget(OverlappingCounter);
		AddDebugTarget(OverlappingDishwasher);
		AddDebugTarget(OverlappingAttractCounter);
		AddDebugTarget(OverlappingGate);
		AddDebugTarget(OverlappingGuest);
		AddDebugTarget(OverlappingChairSeat);
		AddDebugTarget(OverlappingResearchStove);

		const FVector EyeLoc = GetActorLocation() + FVector(0.f, 0.f, 60.f);
		for (AActor* DebugTarget : DebugTargets)
		{
			const FBox B = DebugTarget->GetComponentsBoundingBox(true);
			const FVector TP = B.IsValid ? B.GetClosestPointTo(EyeLoc) : DebugTarget->GetActorLocation();
			const bool bClear = HasInteractionLineOfSight(DebugTarget);
			DrawDebugLine(GetWorld(), EyeLoc, TP, bClear ? FColor::Green : FColor::Red, false, 0.f, 0, 2.f);
			DrawDebugBox(GetWorld(), B.GetCenter(), B.GetExtent(), FColor::Yellow, false, 0.f, 0, 1.f);
		}
	}

 }

 void AShootCharacter::PredictAndDrawParabola()
 {
 	// 清除之前的绘制
 	ClearPrediction();
 	// 计算发射参数
 	FVector StartLocation = GetLaunchLocation();
 	FVector LaunchVelocity = GetLaunchVelocity();
    
 	// 设置预测参数
 	FPredictProjectilePathParams PredictParams;
 	PredictParams.StartLocation = StartLocation;
 	PredictParams.LaunchVelocity = LaunchVelocity;
 	PredictParams.ProjectileRadius = 20.0f; // 预测碰撞的半径
 	PredictParams.MaxSimTime = MaxPredictionTime;
 	PredictParams.bTraceWithCollision = true;
 	PredictParams.bTraceWithChannel = true;
 	PredictParams.TraceChannel = ECC_WorldStatic;
 	PredictParams.SimFrequency = 15.0f; // 模拟频率
 	PredictParams.OverrideGravityZ = GetWorld()->GetGravityZ() ;
 	//PredictParams.DrawDebugType = EDrawDebugTrace::None;
 	PredictParams.DrawDebugType = EDrawDebugTrace::ForDuration; // 我们自定义绘制
 	PredictParams.DrawDebugTime = 0.0f;
    
 	// 执行预测
 	FPredictProjectilePathResult PredictResult;
 	bool bSuccess = UGameplayStatics::PredictProjectilePath(
		 GetWorld(),
		 PredictParams,
		 PredictResult
	 );
    
 	if (bSuccess)
 	{
 		CurrentPathPoints.Empty();
        
 		// 存储路径点
 		for (const FPredictProjectilePathPointData& PointData : PredictResult.PathData)
 		{
 			CurrentPathPoints.Add(PointData.Location);
 		}
        
 		// 绘制抛物线
 		//DrawPredictCurve(CurrentPathPoints);
 	}
 	
 }

 void AShootCharacter::ClearPrediction()
 {
 	// 清除调试绘制（在下一帧会自动清除）
 	CurrentPathPoints.Empty();
    
 }

 void AShootCharacter::DrawPredictCurve(const TArray<FVector>& PathPoints)
 {
 	if (!GetWorld() || PathPoints.Num() < 2) return;
    
 	// 绘制点
 	for (int32 i = 0; i < PathPoints.Num(); i++)
 	{
 		DrawDebugPoint(
			 GetWorld(),
			 PathPoints[i],
			 PointSize,
			 PointColor,
			 false,
			 0.0f
		 );
        
 		// 绘制连线
 		if (bDrawLines && i > 0)
 		{
 			DrawDebugLine(
				 GetWorld(),
				 PathPoints[i-1],
				 PathPoints[i],
				 LineColor,
				 false,
				 0.0f,
				 0,
				 2.0f
			 );
 		}
 	}
    
 	// 绘制起点和终点
 	if (PathPoints.Num() > 0)
 	{
 		// 起点
 		DrawDebugSphere(
			 GetWorld(),
			 PathPoints[0],
			 15.0f,
			 12,
			 FColor::Green,
			 false,
			 0.0f
		 );
        
 		// 终点
 		DrawDebugSphere(
			 GetWorld(),
			 PathPoints.Last(),
			 15.0f,
			 12,
			 FColor::Red,
			 false,
			 0.0f
		 );
 	}
 }

 FVector AShootCharacter::GetLaunchVelocity() const
 {
	if (PickingItem == nullptr)
		return FVector::ZeroVector;
 	APlayerController* PC = Cast<APlayerController>(GetController());
	FRotator ActorRotation = GetActorRotation();
 	FRotator CameraRotation = PC->PlayerCameraManager->GetCameraRotation();

 	// 计算Pitch差
 	float PitchDiff = FMath::FindDeltaAngleDegrees(ActorRotation.Pitch, CameraRotation.Pitch); //根据镜头
 	PitchDiff = 30.0f;//CurrentAngle;//直接拿鼠标控制
 	
 	float Length = PickingItem->ImpuseIntensity ;//+ CurrentAngle * 10.0f ;
 	FRotator PitchRotator(PitchDiff, 0, 0);
 	// 3. 组合旋转：先应用角色朝向，再叠加俯仰
 	FRotator FinalRotator = ActorRotation + PitchRotator;
 	// 使用 RotateAngleAxis 绕特定轴旋转
 	FVector RotatedVector = FinalRotator.Vector();
 	
 	//FString DebugText = FString::Printf(TEXT("TargetVector = %f\n "),PitchDiff);
 	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, DebugText);
 	//DrawDebugDirectionalArrow(GetWorld(),GetLaunchLocation(),GetLaunchLocation() + RotatedVector* Length,10.0f,FColor::Red,false,-1.0f, 0,2.0f);
 	//DrawDebugDirectionalArrow(GetWorld(),GetLaunchLocation(),GetLaunchLocation() + GetActorForwardVector() * (Length+100.f),10.0f,FColor::Blue,false,-1.0f, 0,2.0f);

 	return RotatedVector * Length;
 }

 FVector AShootCharacter::GetLaunchLocation() const
 {
 	if (PickingItem)
 	{
 		return PickingItem->GetActorLocation() + FVector(0, 0, 50.f);//稍微抬高
 	}
 	return FVector::ZeroVector;
 }

 void AShootCharacter::UpdateCharacterRotation(float DeltaTime)
 {
 	APlayerController* PC = Cast<APlayerController>(GetController());
 	FRotator CameraRotation = PC->PlayerCameraManager->GetCameraRotation();

 	// 获取角色当前旋转（只考虑Yaw）
 	FRotator CharacterRotation = GetActorRotation();
    
 	// 计算角度差,// 标准化角度差到 -180 到 180 之间
 	float YawDiff = FMath::FindDeltaAngleDegrees(CharacterRotation.Yaw, CameraRotation.Yaw);
    
 	// 判断是否超过阈值
 	if (FMath::Abs(YawDiff) > RotationThreshold)
 	{
 		// 设置目标旋转（保持Pitch和Roll不变）
 		TargetTurnRotation = FRotator(CharacterRotation.Pitch, CameraRotation.Yaw, CharacterRotation.Roll);
 		bNeedRotation = true;
 	}
    
 	// 执行旋转
 	if (bNeedRotation)
 	{
 		FRotator NewRotation = FMath::RInterpTo(CharacterRotation, TargetTurnRotation, DeltaTime, RotationSpeed);
 		SetActorRotation(NewRotation);
        
 		// 检查是否旋转到位
 		float RemainingDiff = FMath::FindDeltaAngleDegrees(NewRotation.Yaw, TargetTurnRotation.Yaw);
 		if (FMath::Abs(RemainingDiff) < 1.0f)
 		{
 			bNeedRotation = false;
 		}
 	}
 }

 void AShootCharacter::UpdateCountdown()
 {
 	OnUpdateCountdownEvent();
 	// 倒计时减少
 	CountdownTime--;
 	// 检查是否结束
 	if (CountdownTime < 0)
 	{
 		// 清除计时器
 		GetWorldTimerManager().ClearTimer(CountdownTimerHandle);
        
 		// 执行倒计时结束逻辑
 		OnCountdownFinished();
 	}
 	
 }

 void AShootCharacter::OnCountdownFinished()
 {
 	OnCountdownFinishedEvent();
 	if (FreezeInput)
 	{
 		if (APlayerController* APC = Cast<APlayerController>(GetController()))
 		{
 			APC->ResetIgnoreMoveInput();
 		}
 		
 	}
 	if (GrayscaleMID)
 	{
 		GetFollowCamera()->PostProcessSettings.AddBlendable(GrayscaleMID, 0.0f);
 	}
 	// 在这里添加倒计时结束后的代码
 	// 例如：开始游戏、释放技能、播放特效等
 }
