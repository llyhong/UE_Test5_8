// Fill out your copyright notice in the Description page of Project Settings.

#include "Machine/ResearchStove.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Item/ItemActor.h"
#include "ShootCharacter.h"
#include "Game/BaseGameInstance.h"

AResearchStove::AResearchStove()
{
	// 需要 Tick 推进研究计时
	PrimaryActorTick.bCanEverTick = true;

	// 炉子外观模型作为根组件，交互球挂在其上
	StoveMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StoveMesh"));
	RootComponent = StoveMesh;

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(RootComponent);
	InteractionSphere->SetSphereRadius(160.f);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	InteractionSphere->SetGenerateOverlapEvents(true);

	// 交互提示控件（如"按 E 投入食材研究"）：默认隐藏，靠近时显示；Widget Class 由蓝图在组件上配置
	TipsWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("TipsWidget"));
	TipsWidget->SetupAttachment(RootComponent);
	TipsWidget->SetVisibility(false);
}

void AResearchStove::BeginPlay()
{
	Super::BeginPlay();

	InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &AResearchStove::OnInteractionOverlap);
	InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &AResearchStove::OnInteractionEndOverlap);
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
	}
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
		UE_LOG(LogTemp, Log, TEXT("[ResearchStove] 投入失败：加入 ItemID=%d 后无法凑成任何配方（炉内 %d 份）"),
			ItemID, StoredItemIDs.Num());
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
	ActiveRecipeIndex = INDEX_NONE;
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

	UE_LOG(LogTemp, Log, TEXT("[ResearchStove] 开始研究：%s -> 解锁菜谱 FoodID=%d，耗时 %.1f 秒"),
		*Recipe.RecipeName, Recipe.UnlockFoodID, ResearchRemainTime);

	OnResearchStart(Recipe.UnlockFoodID, ResearchRemainTime);

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

	const int32 Idx = ActiveRecipeIndex;
	if (!Recipes.IsValidIndex(Idx))
	{
		ActiveRecipeIndex = INDEX_NONE;
		return;
	}

	const int32 UnlockFoodID = Recipes[Idx].UnlockFoodID;
	UnlockRecipe(UnlockFoodID);
	OnResearchComplete(UnlockFoodID);

	// 研究完成后清空炉内食材
	if (bClearAfterResearch)
	{
		StoredItemIDs.Empty();
	}
	ActiveRecipeIndex = INDEX_NONE;
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

void AResearchStove::OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AShootCharacter* Character = Cast<AShootCharacter>(OtherActor);
	if (!Character)
	{
		return;
	}

	Character->SetOverlappingResearchStove(this);

	// 当提示控件未被其它玩家占用时，显示"按 E 投入食材研究"提示
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

void AResearchStove::OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	AShootCharacter* Character = Cast<AShootCharacter>(OtherActor);
	if (!Character)
	{
		return;
	}

	if (Character->GetOverlappingResearchStove() == this)
	{
		Character->SetOverlappingResearchStove(nullptr);
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
