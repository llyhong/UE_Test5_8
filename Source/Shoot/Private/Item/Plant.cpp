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

	ApplyStageMesh(0);
}

void APlant::BeginPlay()
{
	Super::BeginPlay();

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
		// 初始水分：需要浇水则满水，否则置 0（不影响生长）
		WaterLevel = Info->bRequiresWater ? Info->WaterMax : 0.f;
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
	PlantMesh->SetRelativeScale3D(Stage.StageScale);
}

void APlant::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

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

	// 浇水消耗
	if (Info->bRequiresWater)
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
		StageTimer += DeltaTime * FMath::Max(0.0001f, Info->GrowthSpeed);

		const float StageDuration = Info->GrowthStages[CurrentStageIndex].StageDuration;
		if (StageDuration > 0.f && StageTimer >= StageDuration)
		{
			AdvanceStage();
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

	// 到达最后一个阶段即结果阶段 -> 在树上生成果实（可见），并通知可收获
	if (IsFruiting())
	{
		SpawnFruit(); // 果实直接长在树上，每个果实自行启动熟度计时
		OnReadyToHarvest();
		UpdateHarvestHintOutline(); // 玩家已在范围内时，立即点亮“可采摘”描边提示
	}
}

void APlant::Water(float Amount)
{
	const FPlantInfo* Info = GetPlantInfo();
	if (!Info)
	{
		return;
	}
	WaterLevel = FMath::Min(Info->WaterMax, WaterLevel + Amount);
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

	// 新行为：按 E 直接把果实放入玩家背包，然后植被消失（一次性采摘，不再自动重结果）
	if (bHarvestToInventory && InteractingCharacter)
	{
		const int32 Yield = (HarvestYield > 0) ? HarvestYield : FMath::Max(1, Info->FruitCount);
		const EIngredientQuality Quality = GetCurrentQuality();

		// 清理树上已挂的果实，避免与入背包的果实重复（这些会随植被一起销毁）
		for (AItemActor* Fruit : SpawnedFruits)
		{
			if (Fruit)
			{
				Fruit->StopRipening();
				Fruit->Destroy();
			}
		}
		SpawnedFruits.Empty();
		bHarvested = true;

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
				if (!InteractingCharacter->AddItemToInventory(Fruit))
				{
					// 背包已满等情况：入包失败则直接销毁，避免世界残留
					Fruit->Destroy();
				}
			}
		}

		// 解除与角色的交互注册，避免悬空引用
		if (InteractingCharacter->GetOverlappingPlant() == this)
		{
			InteractingCharacter->SetOverlappingPlant(nullptr);
		}
		InteractingCharacter = nullptr;

		// 植被消失
		Destroy();
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
	bHarvested = true;

	// 摘果后自动重新结果：把果实重新挂回树上（按 RefruitInterval 逐个或一次性），新果实各自从“未成熟”重新计时
	if (Info->bAutoRefruit)
	{
		bHarvested = false;
		bPest = false;
		PestTimer = 0.f;

		if (Info->RefruitInterval > 0.f)
		{
			// 每隔 RefruitInterval 秒长出一个，直到达到 FruitCount（逐个重结果）
			GetWorldTimerManager().SetTimer(RefruitTimerHandle, this, &APlant::RefruitOneFruit, Info->RefruitInterval, true);
		}
		else
		{
			// 立即一次性重新结满
			SpawnFruit();
			OnReadyToHarvest();
		}
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
	if (!Info || !GetWorld() || !Info->FruitClass || !PlantMesh)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AItemActor* Fruit = GetWorld()->SpawnActor<AItemActor>(Info->FruitClass, GetActorTransform(), SpawnParams);
	if (Fruit)
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

		UE_LOG(LogShoot, Log, TEXT("[%s] 果实 %d 挂点 SocketName=%s, 插槽是否存在=%s"), *GetName(), Index, *SocketName.ToString(), PlantMesh->DoesSocketExist(SocketName) ? TEXT("是") : TEXT("否"));

		// 复用 ItemActor 的挂点逻辑，挂到植物模型上对应的插槽
		if (SocketName != NAME_None)
		{
			Fruit->AttachMeshToSocket(PlantMesh, SocketName);
		}

		SpawnedFruits.Add(Fruit);

		// 入背包采摘模式下，树上的果实不再可被玩家单独拾取，仅作为普通场景物件；
		// 采摘由植物在 Harvest 中统一处理入背包。旧模式（挂手上）则保持可拾取。
		Fruit->bPickable = !bHarvestToInventory;

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
		Character->SetOverlappingPlant(this);
		UpdateHarvestHintOutline();
	}
}

void APlant::OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		if (Character->GetOverlappingPlant() == this)
		{
			Character->SetOverlappingPlant(nullptr);
		}
		if (InteractingCharacter == Character)
		{
			InteractingCharacter = nullptr;
		}
		UpdateHarvestHintOutline();
	}
}

void APlant::UpdateHarvestHintOutline()
{
	// 仅当“入背包采摘模式 + 已结果 + 未收获 + 已解锁 + 玩家在范围内 + 是当前正在交互（待采摘）的目标植被”时描边
	const bool bShowHint = bHarvestToInventory
		&& IsFruiting()
		&& !bHarvested
		&& (IngredientUnlockID < 0 || bIngredientUnlocked)
		&& InteractingCharacter != nullptr
		&& InteractingCharacter->GetOverlappingPlant() == this;

	// 仅在状态变化时改写自定义深度，避免每帧无意义调用
	if (bShowHint != bHintOutlineActive)
	{
		bHintOutlineActive = bShowHint;
		AItemActor::SetActorsOutline(this, bShowHint);
	}
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
