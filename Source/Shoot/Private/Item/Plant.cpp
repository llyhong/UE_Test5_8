// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/Plant.h"
#include "Data/PlantInfo.h"
#include "Item/ItemActor.h"
#include "ShootCharacter.h"
#include "Shoot.h"
#include "Game/BaseGameInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"

APlant::APlant()
{
	PrimaryActorTick.bCanEverTick = true;

	PlantMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlantMesh"));
	PlantMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
	RootComponent = PlantMesh;

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(RootComponent);
	InteractionSphere->SetSphereRadius(120.f);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	// 视口里未选中也画出判定球（黄绿=植株），方便摆放时目测采摘范围
	InteractionSphere->ShapeColor = FColor(170, 210, 60);
	InteractionSphere->bDrawOnlyIfSelected = false;

	ApplyStageMesh(0);
}

void APlant::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 编辑器视口预览：改 InteractionRadius 立刻反映到组件上
	ApplyInteractionScale();
}

void APlant::ApplyInteractionScale()
{
	// ============ 统一交互判定尺度 ============
	// 强制按统一标准重设，覆盖蓝图里可能残留的旧半径（各植物蓝图曾经各写各的）
	if (InteractionSphere)
	{
		InteractionSphere->SetSphereRadius(InteractionRadius * InteractionScale::Get(), false);
		InteractionSphere->SetRelativeLocation(FVector(0.f, 0.f, InteractionCenterHeight));
		InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
		InteractionSphere->SetGenerateOverlapEvents(true);
	}
}

void APlant::BeginPlay()
{
	Super::BeginPlay();

	AItemActor::SetActorsOutline(this, false);
	ApplyInteractionScale();

	InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &APlant::OnInteractionOverlap);
	InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &APlant::OnInteractionEndOverlap);

	// 食材解锁：若配置了 IngredientUnlockID(>=0)，则按其是否已解锁决定能否采摘；
	// 并订阅 GameInstance 的解锁广播，运行时解锁后即时开放采摘。
	if (IngredientUnlockID >= 0)
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
		{
			bIngredientUnlocked = GI->IsIngredientUnlocked(IngredientUnlockID);
			GI->OnIngredientsUnlocked.AddDynamic(this, &APlant::HandleIngredientsUnlocked);
		}
		else
		{
			bIngredientUnlocked = false;
		}
		OnPlantUnlockStateChanged(bIngredientUnlocked);
	}

	const FPlantInfo* Info = GetPlantInfo();
	if (Info)
	{
		// 初始水分：需要浇水（bRequiresWater）的植物初始为 0，必须先由玩家浇水才能开始生长；
		// 不需要浇水的植物水分无关紧要，同样置 0。
		WaterLevel = 0.f;
		PestTimer = 0.f;
	}
	else
	{
		WaterLevel = 0.f;
	}

	CurrentStageIndex = 0;
	StageTimer = 0.f;
	bPest = false;
	bHarvested = false;

	ApplyStageMesh(CurrentStageIndex);

	// 诊断：打印本株植物的阶段配置（勾选 bDebug 后可在 Output Log 查看每阶段是否配了模型）
	if (bDebug && Info)
	{
		UE_LOG(LogShoot, Log, TEXT("[%s] 生长阶段数=%d%s"), *GetName(), Info->GrowthStages.Num(),
			Info->GrowthStages.Num() <= 1 ? TEXT("（单阶段：开局即结果，不存在“第二阶段”）") : TEXT(""));
		for (int32 i = 0; i < Info->GrowthStages.Num(); ++i)
		{
			const FPlantGrowthStage& S = Info->GrowthStages[i];
			UE_LOG(LogShoot, Log, TEXT("  阶段%d: 名称=%s 时长=%.1f StageMesh=%s 缩放=%s"),
				i, *S.StageName.ToString(), S.StageDuration,
				S.StageMesh ? *S.StageMesh->GetName() : TEXT("空（保持上一阶段模型）"),
				*S.StageScale.ToString());
		}
	}

	// 若初始即处于结果阶段（如单阶段配置），直接把果实挂到树上
	if (IsFruiting() && SpawnedFruits.Num() == 0)
	{
		SpawnFruit();
	}
}

void APlant::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(RefruitTimerHandle);
	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		GI->OnIngredientsUnlocked.RemoveDynamic(this, &APlant::HandleIngredientsUnlocked);
	}
	Super::EndPlay(EndPlayReason);
}

const FPlantInfo* APlant::GetPlantInfo() const
{
	return PlantInfoRow.GetRow<FPlantInfo>(TEXT("PlantInfo"));
}

void APlant::ApplyStageMesh(int32 StageIndex)
{
	const FPlantInfo* Info = GetPlantInfo();
	if (!Info || !PlantMesh)
	{
		return;
	}
	if (!Info->GrowthStages.IsValidIndex(StageIndex))
	{
		return;
	}

	const FPlantGrowthStage& Stage = Info->GrowthStages[StageIndex];
	if (Stage.StageMesh)
	{
		PlantMesh->SetStaticMesh(Stage.StageMesh);
	}
	else if (bDebug)
	{
		// 该阶段没配模型：按约定保持上一阶段模型（这就是“阶段推进了但模型不变”的常见原因）
		UE_LOG(LogShoot, Warning,
			TEXT("[%s] 阶段 %d（%s）未配置 StageMesh：模型保持上一阶段，仅缩放更新"),
			*GetName(), StageIndex, *Stage.StageName.ToString());
	}
	PlantMesh->SetRelativeScale3D(Stage.StageScale);
}

void APlant::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// ============ 交互范围调试可视化（控制台 Shoot.DebugInteract 1） ============
	// 绿=当前待交互植株，灰=在范围内但不是当前目标
	if (InteractionDebug::IsEnabled() && InteractionSphere)
	{
		const bool bIsCurrent = InteractingCharacter && InteractingCharacter->GetOverlappingPlant() == this;
		const FColor C = bIsCurrent ? InteractionDebug::ColorCurrentTarget() : InteractionDebug::ColorIdle();
		DrawDebugSphere(GetWorld(), InteractionSphere->GetComponentLocation(), InteractionSphere->GetScaledSphereRadius(),
			16, C, false, 0.f, 0, 1.5f);
	}

	// 每帧以“当前目标植被”为准校正采摘描边：确保只有当前待采摘的植被亮起，
	// 其它植被（即使曾因事件顺序残留描边）会被强制关闭。
	UpdateHarvestHintOutline();

// 调试信息：节流打印到屏幕
	if (bDebug)
	{

		DebugPrintTimer += DeltaTime;
		if (DebugPrintTimer >= 0.5f)
		{
			DebugPrintTimer = 0.f;
			PrintDebugInfo();
		}
	}

	const FPlantInfo* Info = GetPlantInfo();
	if (!Info || Info->GrowthStages.Num() == 0)
	{
		return;
	}

	// 浇水消耗：仅在尚未浇满（幼苗期）时按消耗率下降；浇满后保持满水，无需补浇，直接长到结果
	if (Info->bRequiresWater && !bWatered)
	{
		WaterLevel = FMath::Max(0.f, WaterLevel - Info->WaterConsumeRate * DeltaTime);
	}

	// 随机生虫
	if (Info->bCanGetPest && Info->PestInterval > 0.f && !bPest && !bHarvested)
	{
		PestTimer += DeltaTime;
		if (PestTimer >= Info->PestInterval)
		{
			bPest = true;
			PestTimer = 0.f;
			OnPestAppeared();
		}
	}

	// 已收获则停止一切计时
	if (bHarvested)
	{
		return;
	}

	// 结果阶段：不再推进生长阶段。熟度由每个果实自身在其 Tick 中独立累计与广播（见 AItemActor）。
	if (IsFruiting())
	{
		return;
	}

	// 仅在“有水且无虫”时生长
	const bool bHasWater = !Info->bRequiresWater || WaterLevel > 0.f;
	if (bHasWater && !bPest)
	{
		// 采摘后的重新生长延迟：先消耗 RegrowDelayRemaining（此期间停留在幼苗阶段、不推进阶段），
		// 消耗完后才按正常 StageTimer 推进。等效于“幼苗→下一阶段”的时长 = 该阶段 StageDuration + RegrowDelayAfterHarvest。
		if (RegrowDelayRemaining > 0.f)
		{
			RegrowDelayRemaining = FMath::Max(0.f, RegrowDelayRemaining - DeltaTime);
		}
		else
		{
			StageTimer += DeltaTime * FMath::Max(0.0001f, Info->GrowthSpeed);

			// StageDuration <= 0 视为“该阶段立即完成”：避免 DataTable 里把时长配成 0 时，
			// 植物永远卡在当前阶段（模型不切换、也无法进入下一阶段）。
			const float StageDuration = Info->GrowthStages[CurrentStageIndex].StageDuration;
			if (StageDuration <= 0.f || StageTimer >= StageDuration)
			{
				AdvanceStage();
			}
		}
	}

	
}

void APlant::PrintDebugInfo()
{
	if (!GEngine)
	{
		return;
	}

	const FPlantInfo* Info = GetPlantInfo();
	const FString WaterInfo = Info
		? FString::Printf(TEXT("Water:%.0f/%.0f"), WaterLevel, Info->WaterMax)
		: FString(TEXT("Water:--"));

	const FString Msg = FString::Printf(
		TEXT("[Plant] %s  阶段:%d/%d  进度:%.0f%%  %s  虫:%s  %s"),
		*GetCurrentStageName().ToString(),
		CurrentStageIndex + 1,
		Info ? Info->GrowthStages.Num() : 0,
		GetGrowthProgress() * 100.f,
		*WaterInfo,
		bPest ? TEXT("有") : TEXT("无"),
		IsFruiting() ? TEXT("可收获") : (bHarvested ? TEXT("已收获") : TEXT("生长中"))
	);

	GEngine->AddOnScreenDebugMessage(-1, 0.5f, FColor::Green, Msg);
}

void APlant::AdvanceStage()
{
	const FPlantInfo* Info = GetPlantInfo();
	if (!Info)
	{
		return;
	}

	CurrentStageIndex++;
	StageTimer = 0.f;

	// 阶段越界保护
	if (!Info->GrowthStages.IsValidIndex(CurrentStageIndex))
	{
		CurrentStageIndex = Info->GrowthStages.Num() - 1;
	}

	ApplyStageMesh(CurrentStageIndex);

	OnStageChanged(CurrentStageIndex, GetCurrentStageName());
	OnPlantStageChanged.Broadcast(CurrentStageIndex, GetCurrentStageName());

	if (bDebug)
	{
		UE_LOG(LogShoot, Log, TEXT("[%s] 推进到阶段 %d/%d（%s）"),
			*GetName(), CurrentStageIndex + 1, Info->GrowthStages.Num(), *GetCurrentStageName().ToString());
	}

	// 到达最后一个阶段即结果阶段 -> 生成用于采摘/熟度的果实，并通知可收获。
	// （bShowFruitOnPlant=false 时依然会生成，只是不在植物上显示外观，采摘与熟度不受影响）
	if (IsFruiting())
	{
		SpawnFruit(); // 果实直接长在树上，每个果实自行启动熟度计时
		OnReadyToHarvest();
		UpdateHarvestHintOutline(); // 玩家已在范围内时，立即点亮“可采摘”描边提示
	}
}

void APlant::ResetToSeedStage()
{
	// 摘取后回到第一阶段（种子期）：清空收获/害虫/计时状态，恢复生长，使植被重新生长并再次结果
	CurrentStageIndex = 0;
	StageTimer = 0.f;
	WaterLevel = 0.f;
	bPest = false;
	PestTimer = 0.f;
	bHarvested = false;
	bWatered = false;

	// 取消任何残留的逐个重结果定时器
	GetWorldTimerManager().ClearTimer(RefruitTimerHandle);

	ApplyStageMesh(CurrentStageIndex);
	OnStageChanged(CurrentStageIndex, GetCurrentStageName());
	OnPlantStageChanged.Broadcast(CurrentStageIndex, GetCurrentStageName());

	// 单阶段配置（第 0 阶段即结果阶段）：直接重新挂果，否则由 Tick 正常推进阶段直到再次结果
	if (IsFruiting() && SpawnedFruits.Num() == 0)
	{
		SpawnFruit();
		OnReadyToHarvest();
	}
}

void APlant::Water(float /*Amount*/)
{
	const FPlantInfo* Info = GetPlantInfo();
	if (!Info)
	{
		return;
	}
	// 一次浇水即把植被浇满：避免对一株植物反复多次浇水，浇一次即可满足其整个生长周期所需水分；
	// 浇满后置位 bWatered，之后不再消耗水分，长到结果前都无需补浇。
	WaterLevel = Info->WaterMax;
	bWatered = true;
}

bool APlant::NeedsWater() const
{
	const FPlantInfo* Info = GetPlantInfo();
	// 仅当配置需要浇水且当前水位已干涸（<=0）才视为“需要浇水”：
	// 这样一次浇水浇满后，在水分被消耗回 0 之前无法再次浇水，避免对一株植物反复多次浇水。
	return Info && Info->bRequiresWater && WaterLevel <= 0.f;
}

void APlant::RemovePest()
{
	if (bPest)
	{
		bPest = false;
		PestTimer = 0.f;
	}
}

bool APlant::Harvest()
{
	// 食材未解锁：禁止采摘（如关卡目标未达成时玉米田里的玉米不可摘）
	if (IngredientUnlockID >= 0 && !bIngredientUnlocked)
	{
		OnHarvestLocked();
		UE_LOG(LogShoot, Log, TEXT("[%s] 采摘被拒绝：食材 ID=%d 尚未解锁"), *GetName(), IngredientUnlockID);
		return false;
	}

	const FPlantInfo* Info = GetPlantInfo();
	if (!Info || !IsFruiting() || bHarvested || SpawnedFruits.Num() == 0)
	{
		return false;
	}

	// 新行为：按 E 直接把果实放入玩家背包；摘取后不销毁植被，而是重置回第一阶段重新生长结果
	if (bHarvestToInventory && InteractingCharacter)
	{
		const int32 Yield = (HarvestYield > 0) ? HarvestYield : FMath::Max(1, Info->FruitCount);
		const EIngredientQuality Quality = GetCurrentQuality();

		// 清理树上已挂的果实，避免与入背包的果实重复
		for (AItemActor* Fruit : SpawnedFruits)
		{
			if (Fruit)
			{
				Fruit->StopRipening();
				Fruit->Destroy();
			}
		}
		SpawnedFruits.Empty();

		bool bCollectedFruit = false;
		// 生成配置数量的果实并逐个入背包（背包按 ItemClass 堆叠为同一槽位，Quantity=Yield）
		for (int32 i = 0; i < Yield; i++)
		{
			if (!Info->FruitClass)
			{
				UE_LOG(LogShoot, Warning, TEXT("[%s] 采摘入背包失败：DataTable 未配置 FruitClass"), *GetName());
				break;
			}
			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = this;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AItemActor* Fruit = GetWorld()->SpawnActor<AItemActor>(Info->FruitClass, GetActorTransform(), SpawnParams);
			if (Fruit)
			{
				Fruit->SetIngredientQuality(Quality); // 按当前整株熟度写入品质
				if (InteractingCharacter->AddItemToInventory(Fruit))
				{
					bCollectedFruit = true;
				}
				else
				{
					// 背包已满等情况：入包失败则直接销毁，避免世界残留
					Fruit->Destroy();
				}
			}
		}

		if (bCollectedFruit)
		{
			InteractingCharacter->PlayHarvestEffect(PlantMesh->Bounds.Origin);
		}

		// 解除与角色的交互注册，避免悬空引用
		if (InteractingCharacter->GetOverlappingPlant() == this)
		{
			InteractingCharacter->SetOverlappingPlant(nullptr);
		}
		InteractingCharacter = nullptr;

		// 摘取后不销毁植被：立即重置回幼苗阶段重新生长；重新生长前的等待并入“幼苗→下一阶段”的时长（见 Tick：RegrowDelayRemaining）
		ResetToSeedStage();
		RegrowDelayRemaining = Info ? Info->RegrowDelayAfterHarvest : 0.f;
		return true;
	}

	// 旧行为：把树上的果实挂到玩家手上（不收进背包）；无交互角色则直接销毁
	// 采摘瞬间按【每个果实自身】的熟度把品质写入该果实（不同果实可能处于不同熟度阶段），最终影响成品品质与上菜奖励
	if (InteractingCharacter)
	{
		bool bFirst = true;
		for (AItemActor* Fruit : SpawnedFruits)
		{
			if (Fruit)
			{
				// 离树：停止该果实的熟度计时（品质固定），并按其当前熟度写入品质
				Fruit->StopRipening();
				Fruit->SetIngredientQuality(Fruit->GetCurrentQuality());
				InteractingCharacter->HoldItemInHand(Fruit, bFirst); // 第一个作为主手持物，其余也挂到手上
				bFirst = false;
			}
		}
		if (!bFirst)
		{
			InteractingCharacter->PlayHarvestEffect(PlantMesh->Bounds.Origin);
		}
	}
	else
	{
		for (AItemActor* Fruit : SpawnedFruits)
		{
			if (Fruit)
			{
				Fruit->StopRipening();
				Fruit->Destroy();
			}
		}
	}
	SpawnedFruits.Empty();

	// 摘果后：若启用自动重新结果，则立即重置回幼苗阶段重新生长（重新生长前的等待并入“幼苗→下一阶段”时长）；
	// 否则标记为已收获并停止生长（植被保留但不再结果）。
	if (Info->bAutoRefruit)
	{
		ResetToSeedStage();
		RegrowDelayRemaining = Info->RegrowDelayAfterHarvest;
	}
	else
	{
		bHarvested = true;
	}

	return true;
}

void APlant::SpawnFruit()
{
	const FPlantInfo* Info = GetPlantInfo();
	if (!Info || !GetWorld())
	{
		return;
	}

	if (!Info->FruitClass)
	{
		UE_LOG(LogShoot, Warning, TEXT("[%s] SpawnFruit 失败：DataTable 未配置 FruitClass（果实类型）"), *GetName());
		return;
	}

	// 避免重复生成（同一轮结果只生成一次）
	SpawnedFruits.Empty();

	for (int32 i = 0; i < FMath::Max(1, Info->FruitCount); i++)
	{
		SpawnSingleFruit(i);
	}
}

void APlant::SpawnSingleFruit(int32 Index)
{
	const FPlantInfo* Info = GetPlantInfo();
	if (!Info || !GetWorld() || !Info->FruitClass)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AItemActor* Fruit = GetWorld()->SpawnActor<AItemActor>(Info->FruitClass, GetActorTransform(), SpawnParams);
	if (Fruit)
	{
		if (Info->bShowFruitOnPlant && PlantMesh)
		{
			// 果实挂点与数量对齐：多个果实时按 “基础名_索引” 寻找对应插槽（如 Fruit0 / Fruit1）
			FName SocketName = NAME_None;
			if (Info->FruitAttachSocket != NAME_None)
			{
				SocketName = (Info->FruitCount > 1)
					? FName(*FString::Printf(TEXT("%s%d"), *Info->FruitAttachSocket.ToString(), Index))
					: FName(*FString::Printf(TEXT("%s0"), *Info->FruitAttachSocket.ToString()));

				// 索引插槽不存在时，回退到基础插槽名；仍不存在则挂在模型根（不指定插槽）
				if (!PlantMesh->DoesSocketExist(SocketName))
				{
					SocketName = PlantMesh->DoesSocketExist(Info->FruitAttachSocket)
						? Info->FruitAttachSocket
						: NAME_None;
				}
			}

			//UE_LOG(LogShoot, Log, TEXT("[%s] 果实 %d 挂点 SocketName=%s, 插槽是否存在=%s"), *GetName(), Index, *SocketName.ToString(), PlantMesh->DoesSocketExist(SocketName) ? TEXT("是") : TEXT("否"));

			// 复用 ItemActor 的挂点逻辑，挂到植物模型上对应的插槽
			if (SocketName != NAME_None)
			{
				Fruit->AttachMeshToSocket(PlantMesh, SocketName);
			}
		}
		else
		{
			// 不在植物上显示果实：隐藏外观并关闭碰撞；Actor 仍保留作为熟度 / 品质 / 采摘的数据载体
			Fruit->SetActorHiddenInGame(true);
			Fruit->SetActorEnableCollision(false);
		}

		SpawnedFruits.Add(Fruit);

		// 入背包采摘模式下，树上的果实不再可被玩家单独拾取，仅作为普通场景物件；
		// 采摘由植物在 Harvest 中统一处理入背包。旧模式（挂手上）则保持可拾取。
		// 不显示的果实同样不可被单独拾取。
		Fruit->bPickable = Info->bShowFruitOnPlant && !bHarvestToInventory;

		// 写入熟度配置并启动该果实自身的熟度计时（每个果实独立累计，互不影响）
		Fruit->ConfigureAndStartRipening(Info->bEnableRipeness, Info->RipeTime, Info->OverripeTime);
	}
}

void APlant::RefruitOneFruit()
{
	const FPlantInfo* Info = GetPlantInfo();
	if (!Info || !GetWorld())
	{
		GetWorldTimerManager().ClearTimer(RefruitTimerHandle);
		return;
	}

	// 已不在结果阶段、或已结满，则停止定时器
	const int32 TargetCount = FMath::Max(1, Info->FruitCount);
	if (!IsFruiting() || SpawnedFruits.Num() >= TargetCount)
	{
		GetWorldTimerManager().ClearTimer(RefruitTimerHandle);
		return;
	}

	SpawnSingleFruit(SpawnedFruits.Num());
	UE_LOG(LogShoot, Log, TEXT("[%s] 自动重新结果：已长出 %d / %d 个果实"), *GetName(), SpawnedFruits.Num(), TargetCount);

	if (SpawnedFruits.Num() >= TargetCount)
	{
		OnReadyToHarvest();
		GetWorldTimerManager().ClearTimer(RefruitTimerHandle);
	}
}

FName APlant::GetCurrentStageName() const
{
	// 结果阶段：名称反映熟度子阶段（未成熟 / 成熟 / 熟过头），便于 UI 提示当前素材阶段
	if (IsFruiting())
	{
		return GetRipenessStageName();
	}
	const FPlantInfo* Info = GetPlantInfo();
	if (Info && Info->GrowthStages.IsValidIndex(CurrentStageIndex))
	{
		return Info->GrowthStages[CurrentStageIndex].StageName;
	}
	return NAME_None;
}

float APlant::GetGrowthProgress() const
{
	// 结果阶段：进度条改为显示熟度进度（距“熟过头”的进度），复用已有 UI 绑定
	if (IsFruiting())
	{
		return GetRipenessProgress();
	}
	const FPlantInfo* Info = GetPlantInfo();
	if (!Info || !Info->GrowthStages.IsValidIndex(CurrentStageIndex))
	{
		return 0.f;
	}
	const float StageDuration = Info->GrowthStages[CurrentStageIndex].StageDuration;
	if (StageDuration <= 0.f)
	{
		return 1.f;
	}
	return FMath::Clamp(StageTimer / StageDuration, 0.f, 1.f);
}

EIngredientQuality APlant::GetCurrentQuality() const
{
	// 熟度已下沉到每个果实；此处返回代表性（第一个）果实的品质，供整株级 UI 兼容显示。
	// 按单个果实展示时请直接绑定果实的 GetCurrentQuality()。
	if (SpawnedFruits.Num() > 0 && IsValid(SpawnedFruits[0]))
	{
		return SpawnedFruits[0]->GetCurrentQuality();
	}
	return EIngredientQuality::Normal;
}

float APlant::GetRipenessProgress() const
{
	// 返回代表性（第一个）果实的熟度进度，供整株级 UI 兼容显示
	if (SpawnedFruits.Num() > 0 && IsValid(SpawnedFruits[0]))
	{
		return SpawnedFruits[0]->GetRipenessProgress();
	}
	return 0.f;
}

FName APlant::GetRipenessStageName() const
{
	// 返回代表性（第一个）果实的熟度阶段名，供整株级 UI 兼容显示
	if (SpawnedFruits.Num() > 0 && IsValid(SpawnedFruits[0]))
	{
		return SpawnedFruits[0]->GetRipenessStageName();
	}
	return FName(TEXT("结果"));
}

bool APlant::IsFruiting() const
{
	const FPlantInfo* Info = GetPlantInfo();
	if (!Info || Info->GrowthStages.Num() == 0)
	{
		return false;
	}
	// 最后一个阶段视为“结果”阶段
	return CurrentStageIndex >= Info->GrowthStages.Num() - 1;
}

void APlant::OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		InteractingCharacter = Character;
		// 注册进角色的植物候选集：由角色按距离选最近的一株，取代“后进入者胜”
		Character->AddOverlappingPlant(this);
		UpdateHarvestHintOutline();
	}
}

void APlant::OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		// 只注销自己：角色会从剩余候选里重选最近的一株，
		// 不会像以前那样把另一株（仍在范围内）的目标一起清掉
		Character->RemoveOverlappingPlant(this);
		if (InteractingCharacter == Character)
		{
			InteractingCharacter = nullptr;
		}
		// 玩家离开范围：清除浇水提示抑制，下次靠近若仍缺水会重新高亮
		bWaterHintSuppressed = false;
		UpdateHarvestHintOutline();
	}
}

void APlant::UpdateHarvestHintOutline()
{
	// 植株重新变干（水耗光）时清除浇水提示抑制，恢复“需浇水”高亮
	if (WaterLevel <= 0.f)
	{
		bWaterHintSuppressed = false;
	}

	// 采摘提示：入背包采摘模式 + 已结果 + 未收获 + 已解锁 + 是角色选出的唯一交互目标
	const bool bHarvestHint = bHarvestToInventory
		&& IsFruiting()
		&& !bHarvested
		&& (IngredientUnlockID < 0 || bIngredientUnlocked)
		&& bHighlightRequested;

	// 浇水提示：植被还需要浇水——作为“可浇水 / 待浇水”的前置指示（先于浇水动作出现）；
	// 浇水动作完成后会被 bWaterHintSuppressed 抑制，避免“刚浇完”立刻又亮。
	const bool bWaterHint = bHighlightRequested
		&& NeedsWater()
		&& !bWaterHintSuppressed;

	const bool bShowHint = bHarvestHint || bWaterHint;

	// 仅在状态变化时改写自定义深度，避免每帧无意义调用
	if (bShowHint != bHintOutlineActive)
	{
		bHintOutlineActive = bShowHint;
		AItemActor::SetActorsOutline(this, bShowHint);
	}
}

void APlant::SetInteractionHighlight(bool bEnable)
{
	if (bHighlightRequested == bEnable)
	{
		return;
	}
	bHighlightRequested = bEnable;
	// 角色换目标当帧就要生效：熄灭上一株、点亮这一株都走同一条路径
	UpdateHarvestHintOutline();
}

void APlant::SetWaterHintSuppressed(bool bSuppress)
{
	if (bWaterHintSuppressed == bSuppress)
	{
		return;
	}
	bWaterHintSuppressed = bSuppress;
	// 立即刷新描边，使浇完水后高亮当帧取消
	UpdateHarvestHintOutline();
}

void APlant::SetIngredientUnlocked(bool bUnlocked)
{
	if (bIngredientUnlocked == bUnlocked)
	{
		return;
	}
	bIngredientUnlocked = bUnlocked;
	OnPlantUnlockStateChanged(bUnlocked);
	UpdateHarvestHintOutline();
}

void APlant::HandleIngredientsUnlocked(const TArray<int32>& UnlockedIDs)
{
	if (IngredientUnlockID >= 0 && UnlockedIDs.Contains(IngredientUnlockID))
	{
		SetIngredientUnlocked(true);
	}
}
