// Fill out your copyright notice in the Description page of Project Settings.

#include "Machine/ResearchStove.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Item/ItemActor.h"
#include "ShootCharacter.h"
#include "Interaction/InteractionConfig.h"
#include "Game/BaseGameInstance.h"
#include "Game/FoodSystem.h"
#include "UI/DiscoveryToastWidget.h"
#include "UI/ResearchProgressWidget.h"

AResearchStove::AResearchStove()
{
	// 需要 Tick 推进研究计时
	PrimaryActorTick.bCanEverTick = true;

	// 炉子外观模型作为根组件，交互球挂在其上
	StoveMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StoveMesh"));
	RootComponent = StoveMesh;

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(RootComponent);
	InteractionSphere->SetSphereRadius(InteractionRadius * InteractionScale::Get());
	// 视口里未选中也画出判定球，摆放关卡时能直接目测交互范围
	InteractionSphere->ShapeColor = FColor(40, 190, 200);
	InteractionSphere->bDrawOnlyIfSelected = false;
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	InteractionSphere->SetGenerateOverlapEvents(true);

	// 交互提示控件（如"按 E 投入食材研究"）：默认隐藏，靠近时显示；Widget Class 由蓝图在组件上配置
	TipsWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("TipsWidget"));
	TipsWidget->SetupAttachment(RootComponent);
	TipsWidget->SetVisibility(false);

	// 研究进度条：世界空间，指向炉子上方；纯 C++ 控件，不依赖蓝图资产
	ProgressWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("ProgressWidget"));
	ProgressWidgetComp->SetupAttachment(RootComponent);
	ProgressWidgetComp->SetWidgetSpace(EWidgetSpace::World);
	ProgressWidgetComp->SetDrawSize(FVector2D(220.f, 48.f));
	ProgressWidgetComp->SetRelativeLocation(FVector(0.f, 0.f, 180.f));
	ProgressWidgetComp->SetTwoSided(true);
	ProgressWidgetComp->SetVisibility(false);
}

void AResearchStove::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 编辑器视口预览：改 InteractionRadius 立刻反映到组件上，
	// 摆放关卡时看到的球就是运行时真正生效的判定范围
	ApplyInteractionScale();
}

void AResearchStove::ApplyInteractionScale()
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

void AResearchStove::BeginPlay()
{
	Super::BeginPlay();

	// 统一交互尺度：强制覆盖蓝图里残留的旧半径
	ApplyInteractionScale();

	InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &AResearchStove::OnInteractionOverlap);
	InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &AResearchStove::OnInteractionEndOverlap);

	// 运行时创建进度条控件：编辑器里不需要预先配 WBP
	if (ProgressWidgetComp)
	{
		ProgressWidget = CreateWidget<UResearchProgressWidget>(GetWorld(), UResearchProgressWidget::StaticClass());
		if (ProgressWidget)
		{
			ProgressWidgetComp->SetWidget(ProgressWidget);
		}
		else
		{
			ProgressWidgetComp->SetVisibility(false);
		}
	}
}

void AResearchStove::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bResearching)
	{
		return;
	}

	ResearchRemainTime -= DeltaTime;
	if (ResearchRemainTime <= 0.f)
	{
		ResearchRemainTime = 0.f;
		FinishResearch();
		return;
	}

	UpdateProgressWidget();
}

bool AResearchStove::AddFood(AItemActor* Food)
{
	if (!Food)
	{
		OnResearchFailed(TEXT("NoFood"));
		return false;
	}

	// 研究中不接受新的食材
	if (bResearching)
	{
		UE_LOG(LogTemp, Log, TEXT("[ResearchStove] 投入失败：正在研究中"));
		OnResearchFailed(TEXT("Busy"));
		return false;
	}

	// 空碟/脏碟不是食材，不能投入研究
	if (Food->bIsEmptyPlate || Food->bIsDirtyPlate)
	{
		UE_LOG(LogTemp, Log, TEXT("[ResearchStove] 投入失败：碟子不能用于研究"));
		OnResearchFailed(TEXT("NoItemID"));
		return false;
	}

	const int32 ItemID = Food->GetItemID();
	if (ItemID < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ResearchStove] 投入失败：物品(%s)没有有效 ItemID（请配置 ItemInfoRow）"), *Food->GetName());
		OnResearchFailed(TEXT("NoItemID"));
		return false;
	}

	// 只有当投入后仍有可能凑成某条配方时才接受，避免玩家把食材白白扔进炉里
	if (!CanAcceptItem(ItemID))
	{
		UE_LOG(LogTemp, Log, TEXT("[ResearchStove] 投入失败：加入 ItemID=%d 后无法凑成任何新配方（炉内 %d 份）"),
			ItemID, StoredItemIDs.Num());

		// 区分"配方全研究完了"与"这个食材不对"，并把离达成最近的配方缺什么告诉玩家，
		// 否则玩家只能盲试（配方是数据表里手配的组合，没有提示几乎猜不到）
		TMap<int32, int32> MissingItems;
		int32 BestRecipeIndex = INDEX_NONE;
		if (GetMissingItemsForBestRecipe(MissingItems, BestRecipeIndex))
		{
			const FString MissingText = DescribeMissingItems(MissingItems);
			const FString RecipeName = Recipes[BestRecipeIndex].RecipeName.IsEmpty()
				? FString::Printf(TEXT("配方 %d"), BestRecipeIndex + 1)
				: Recipes[BestRecipeIndex].RecipeName;
			NotifyLocalPlayers(TEXT("研究炉"),
				FString::Printf(TEXT("这个食材配不上炉里的组合。最近的配方「%s」%s"),
					*RecipeName,
					MissingText.IsEmpty() ? TEXT("已经凑齐了，再投入一次即可开始研究") : *FString::Printf(TEXT("还缺：%s"), *MissingText)),
				FLinearColor(0.95f, 0.78f, 0.35f, 1.f));
		}
		else
		{
			NotifyLocalPlayers(TEXT("研究炉"), TEXT("所有配方都已研究完成。"), FLinearColor(0.55f, 0.9f, 0.6f, 1.f));
			OnResearchFailed(TEXT("AllResearched"));
			return false;
		}

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow,
				TEXT("[研究炉] 这个食材凑不出任何新配方（可先清空炉内食材）"));
		}
		OnResearchFailed(TEXT("NoRecipe"));
		return false;
	}

	// 食材投入炉中即被消耗
	StoredItemIDs.Add(ItemID);
	Food->Destroy();

	UE_LOG(LogTemp, Log, TEXT("[ResearchStove] 投入食材 ItemID=%d，炉内共 %d 份"), ItemID, StoredItemIDs.Num());
	OnFoodAdded(ItemID, StoredItemIDs.Num());

	// 与某条配方完全匹配则自动开始研究
	const int32 MatchIndex = FindMatchingRecipeIndex();
	if (MatchIndex != INDEX_NONE)
	{
		StartResearch(MatchIndex);
	}

	return true;
}

void AResearchStove::ClearFoods()
{
	StoredItemIDs.Empty();
	bResearching = false;
	ResearchRemainTime = 0.f;
	ResearchTotalTime = 0.f;
	ActiveRecipeIndex = INDEX_NONE;
	UpdateProgressWidget();
}

int32 AResearchStove::FindMatchingRecipeIndex() const
{
	if (StoredItemIDs.Num() == 0)
	{
		return INDEX_NONE;
	}

	// 统计当前炉内各食材数量
	TMap<int32, int32> StoredCount;
	for (const int32 ID : StoredItemIDs)
	{
		StoredCount.FindOrAdd(ID)++;
	}

	for (int32 i = 0; i < Recipes.Num(); ++i)
	{
		const FResearchRecipe& Recipe = Recipes[i];
		if (Recipe.RequiredItems.Num() == 0)
		{
			continue;
		}

		// 已解锁的菜谱不再参与匹配：否则同一组合会反复研究、白白烧掉食材
		if (IsRecipeAlreadyUnlocked(Recipe.UnlockFoodID))
		{
			continue;
		}

		bool bMatch = (StoredCount.Num() == Recipe.RequiredItems.Num());
		if (bMatch)
		{
			for (const TPair<int32, int32>& Require : Recipe.RequiredItems)
			{
				const int32* Found = StoredCount.Find(Require.Key);
				if (!Found || *Found != Require.Value)
				{
					bMatch = false;
					break;
				}
			}
		}

		if (bMatch)
		{
			return i;
		}
	}

	return INDEX_NONE;
}

bool AResearchStove::CanAcceptItem(int32 ItemID) const
{
	if (Recipes.Num() == 0)
	{
		return false; // 未配置任何配方：不接受投入，避免白白销毁食材
	}

	// 模拟"投入之后"的炉内食材数量
	TMap<int32, int32> Counts;
	for (const int32 ID : StoredItemIDs)
	{
		Counts.FindOrAdd(ID)++;
	}
	Counts.FindOrAdd(ItemID)++;

	for (const FResearchRecipe& Recipe : Recipes)
	{
		if (Recipe.RequiredItems.Num() == 0)
		{
			continue;
		}

		if (IsRecipeAlreadyUnlocked(Recipe.UnlockFoodID))
		{
			continue;
		}

		// 该配方在"容忍多余食材"意义下仍可达成：每种已投入食材都被该配方需要，且数量不超过需求
		bool bPossible = true;
		for (const TPair<int32, int32>& Have : Counts)
		{
			const int32* Need = Recipe.RequiredItems.Find(Have.Key);
			if (!Need || Have.Value > *Need)
			{
				bPossible = false;
				break;
			}
		}

		if (bPossible)
		{
			return true;
		}
	}

	return false;
}

bool AResearchStove::GetMissingItemsForBestRecipe(TMap<int32, int32>& OutMissingItems, int32& OutRecipeIndex) const
{
	OutMissingItems.Reset();
	OutRecipeIndex = INDEX_NONE;

	TMap<int32, int32> StoredCount;
	for (const int32 ID : StoredItemIDs)
	{
		StoredCount.FindOrAdd(ID)++;
	}

	int32 BestMissingTotal = TNumericLimits<int32>::Max();
	for (int32 i = 0; i < Recipes.Num(); ++i)
	{
		const FResearchRecipe& Recipe = Recipes[i];
		if (Recipe.RequiredItems.Num() == 0)
		{
			continue;
		}
		if (IsRecipeAlreadyUnlocked(Recipe.UnlockFoodID))
		{
			continue;
		}

		TMap<int32, int32> Missing;
		int32 MissingTotal = 0;
		for (const TPair<int32, int32>& Require : Recipe.RequiredItems)
		{
			const int32* Have = StoredCount.Find(Require.Key);
			const int32 Lack = Require.Value - (Have ? *Have : 0);
			if (Lack > 0)
			{
				Missing.Add(Require.Key, Lack);
				MissingTotal += Lack;
			}
		}

		if (MissingTotal < BestMissingTotal)
		{
			BestMissingTotal = MissingTotal;
			OutRecipeIndex = i;
			OutMissingItems = MoveTemp(Missing);
		}
	}

	return OutRecipeIndex != INDEX_NONE;
}

void AResearchStove::StartResearch(int32 RecipeIndex)
{
	if (!Recipes.IsValidIndex(RecipeIndex))
	{
		return;
	}

	const FResearchRecipe& Recipe = Recipes[RecipeIndex];
	bResearching = true;
	ActiveRecipeIndex = RecipeIndex;
	ResearchRemainTime = FMath::Max(0.f, Recipe.ResearchTime);
	ResearchTotalTime = ResearchRemainTime;

	UE_LOG(LogTemp, Log, TEXT("[ResearchStove] 开始研究：%s -> 解锁菜谱 FoodID=%d，耗时 %.1f 秒"),
		*Recipe.RecipeName, Recipe.UnlockFoodID, ResearchRemainTime);

	OnResearchStart(Recipe.UnlockFoodID, ResearchRemainTime);
	UpdateProgressWidget();

	// 耗时为 0：立即完成
	if (ResearchRemainTime <= 0.f)
	{
		FinishResearch();
	}
}

void AResearchStove::FinishResearch()
{
	bResearching = false;
	ResearchRemainTime = 0.f;
	ResearchTotalTime = 0.f;

	const int32 Idx = ActiveRecipeIndex;
	if (!Recipes.IsValidIndex(Idx))
	{
		ActiveRecipeIndex = INDEX_NONE;
		ResearchTotalTime = 0.f;
		UpdateProgressWidget();
		return;
	}

	const int32 UnlockFoodID = Recipes[Idx].UnlockFoodID;
	UnlockRecipe(UnlockFoodID);
	OnResearchComplete(UnlockFoodID);

	// 研究完成反馈：菜名 + "已加入菜单"，让玩家知道接下来客人会点这道菜
	FString FoodName = FString::Printf(TEXT("菜品 %d"), UnlockFoodID);
	if (const UBaseGameInstance* GI = GetWorld() ? Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()) : nullptr)
	{
		if (GI->foodSystem)
		{
			const FMenuData Menu = GI->foodSystem->GetMenuData(UnlockFoodID);
			if (Menu.FoodID >= 0 && !Menu.FoodName.IsEmpty())
			{
				FoodName = Menu.FoodName;
			}
		}
	}
	NotifyLocalPlayers(FString::Printf(TEXT("新菜谱 · %s"), *FoodName),
		TEXT("已加入菜单：客人现在可能点这道菜，灶台也能做了。"), FLinearColor(0.55f, 0.9f, 0.6f, 1.f));

	// 研究完成后清空炉内食材
	if (bClearAfterResearch)
	{
		StoredItemIDs.Empty();
	}
	ActiveRecipeIndex = INDEX_NONE;
	UpdateProgressWidget();
}

void AResearchStove::UnlockRecipe(int32 FoodID)
{
	if (FoodID < 0)
	{
		return;
	}

	UBaseGameInstance* GI = GetWorld() ? Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()) : nullptr;
	if (!GI)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ResearchStove] 解锁失败：找不到 UBaseGameInstance"));
		return;
	}

	TArray<int32> IDs;
	IDs.Add(FoodID);
	GI->UnlockRecipes(IDs);

	UE_LOG(LogTemp, Log, TEXT("[ResearchStove] 已解锁菜谱 FoodID=%d"), FoodID);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
			FString::Printf(TEXT("[研究炉] 新菜谱已解锁！FoodID = %d"), FoodID));
	}
}

bool AResearchStove::IsRecipeAlreadyUnlocked(int32 FoodID) const
{
	if (FoodID < 0)
	{
		return false;
	}
	const UBaseGameInstance* GI = GetWorld() ? Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()) : nullptr;
	// 查不到 GameInstance 时按未解锁处理：研究功能不能因为初始化顺序被整体关掉
	return GI && GI->IsRecipeUnlocked(FoodID);
}

FString AResearchStove::DescribeMissingItems(const TMap<int32, int32>& MissingItems) const
{
	const UBaseGameInstance* GI = GetWorld() ? Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()) : nullptr;
	UFoodSystem* FS = GI ? GI->foodSystem.Get() : nullptr;

	TArray<FString> Parts;
	for (const TPair<int32, int32>& Pair : MissingItems)
	{
		const FString ItemName = FS ? FS->GetItemDisplayName(Pair.Key) : FString::Printf(TEXT("食材%d"), Pair.Key);
		Parts.Add(FString::Printf(TEXT("%s×%d"), *ItemName, Pair.Value));
	}
	return FString::Join(Parts, TEXT("、"));
}

void AResearchStove::NotifyLocalPlayers(const FString& Title, const FString& Body, FLinearColor Accent) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (PC->IsLocalController())
			{
				UDiscoveryToastWidget::ShowMessage(PC, Title, Body, Accent);
			}
		}
	}
}

void AResearchStove::UpdateProgressWidget()
{
	if (!ProgressWidgetComp)
	{
		return;
	}

	const bool bShow = bResearching && ResearchTotalTime > 0.f;
	ProgressWidgetComp->SetVisibility(bShow);
	if (!bShow)
	{
		return;
	}

	if (ProgressWidget)
	{
		ProgressWidget->SetProgress(ResearchRemainTime, ResearchTotalTime);
	}

	// 游戏相机接近俯视，世界空间控件保持竖直时几乎是侧对屏幕，只剩一条黑边；
	// 每帧转向相机才能看清进度条
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC && PC->PlayerCameraManager)
	{
		const FVector ToCamera = PC->PlayerCameraManager->GetCameraLocation() - ProgressWidgetComp->GetComponentLocation();
		ProgressWidgetComp->SetWorldRotation(ToCamera.Rotation());
	}
}

void AResearchStove::OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AShootCharacter* Character = Cast<AShootCharacter>(OtherActor);
	if (!Character)
	{
		return;
	}

	Character->AddOverlappingResearchStove(this);

	// 当提示控件未被其它玩家占用时，显示"按 E 投入食材研究"提示。
	// 默认关闭：E 提示统一由角色显示（每个本地玩家一份），TipsWidget 只做位置锚点。
	if (LocalPlayer == nullptr && TipsWidget && InteractionPromptTuning::UseDeviceOwnPromptTips())
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

void AResearchStove::OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	AShootCharacter* Character = Cast<AShootCharacter>(OtherActor);
	if (!Character)
	{
		return;
	}

	// 只注销自己：多台研究炉范围交叠时，另一台仍是候选目标
	Character->RemoveOverlappingResearchStove(this);

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
