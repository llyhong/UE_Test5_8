// Fill out your copyright notice in the Description page of Project Settings.


#include "Machine/CookingMachine.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"
#include "Interfaces/PickupInterface.h"
#include "Game/BaseGameInstance.h"
#include "Game/EconomySystem.h"
#include "Item/ItemActor.h"

// Sets default values
ACookingMachine::ACookingMachine()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CookingMeshComponent"));
	ItemMesh->SetupAttachment(GetRootComponent());
	RootComponent = ItemMesh;
	
	BoxCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("Box Collision"));
	BoxCollision->SetupAttachment(GetRootComponent());
	BoxCollision->SetGenerateOverlapEvents(true);
	BoxCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BoxCollision->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Overlap);
	//BoxCollision->SetCollisionResponseToChannel(ECollisionChannel::ECC_Pawn, ECollisionResponse::ECR_Ignore);

	TipsWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("TipsWidget"));
	TipsWidget->SetupAttachment(GetRootComponent());
	TipsWidget->SetVisibility(false);

	// —— 生成位置标记 ——
	SpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("SpawnPoint"));
	SpawnPoint->SetupAttachment(GetRootComponent());
}


// Called when the game starts or when spawned
void ACookingMachine::BeginPlay()
{
	ResetFoodData();
	Tags.Add("Machine");
	Super::BeginPlay();
	BoxCollision->OnComponentBeginOverlap.AddDynamic(this, &ACookingMachine::OnBoxOverlap);
	BoxCollision->OnComponentEndOverlap.AddDynamic(this, &ACookingMachine::OnBoxEndOverlap);
}

void ACookingMachine::ResetFoodData()
{
	TargetMenuData.FoodID = -2;
	TargetMateiralCount = 0;
	CurFoodCount = 0;
	CurCookingTime = 0;
	CurRepairTime = 0;
	Repairing = false;
	IsFull = false;
	OnFire = false;
	IsCooking = false;
	IsFinish = false;
	OverCookTime = 0;
	PotentialMenuIDs.Empty(4);
	FoodMaterial.Empty(6);
}
bool ACookingMachine::FindMatchMenu(int32 FoodMaterialID,FMenuData& matchMenuData)
{
	float minCookingTime = MAX_FLT;
	int minCookingMateiralCount = 10000;
	bool canFind = false;
	for (int i = 0; i < MenuIDsArray.Num(); i++)
	{
		const FMenuData* menuDataPtr = MenuIDsArray[i].GetRow<FMenuData>(TEXT("CookingMachine::FindMatchMenu"));
		if (!menuDataPtr)
		{
			continue;
		}
		const FMenuData& menuData = *menuDataPtr;
		//最少食材
		if (menuData.FoodID != -1 && menuData.MenuItems.Contains(FoodMaterialID) && menuData.MenuItems.Num() <= minCookingMateiralCount  )
		{
			//最短时间
			if (menuData.CookingTime < minCookingTime)
			{
				matchMenuData = menuData;
				if (!canFind)
					canFind = true;
			}
		}
	}
	return canFind;
}

bool ACookingMachine::CheckMenu(int32 FoodMaterialID,FMenuData& matchMenuData)
{
	bool canFind = false;
	auto curFoodMaterialID = FoodMaterial;
	if (!curFoodMaterialID.Contains(FoodMaterialID))
		curFoodMaterialID.Add(FoodMaterialID,1);
	else
		curFoodMaterialID[FoodMaterialID] += 1;
	float minCookingTime = MAX_FLT;
	int minCookingMateiralCount = 10000;
	
	for (int i = 0; i < MenuIDsArray.Num(); i++) //遍历可制作菜单
	{
		// 先将 FDataTableRowHandle 拆开，取出 RowName
		const FName RowName = MenuIDsArray[i].RowName;
		const FMenuData* menuDataPtr = MenuIDsArray[i].GetRow<FMenuData>(*RowName.ToString());
		if (!menuDataPtr)
		{
			const FString DTName = MenuIDsArray[i].DataTable ? MenuIDsArray[i].DataTable->GetName() : TEXT("NULL");
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
				FString::Printf(TEXT("[CheckMenu] 第%d项无效 -> DataTable:%s, RowName:%s (DataTable为空或RowName不存在)"),
					i, *DTName, *RowName.ToString()));
			continue;
		}
		const FMenuData& menuData = *menuDataPtr;
		if (menuData.FoodID >=0 && menuData.MenuItems.Num() >= curFoodMaterialID.Num() )
		{
			bool match = true;
			//找包含当前食材都包括的菜单
			for (TPair<int, int>& foodMat : curFoodMaterialID) //遍历当前容器食材
			{
				int foodMatID = foodMat.Key;
				int foodMatNum = foodMat.Value;
				if (!menuData.MenuItems.Contains(foodMatID) || menuData.MenuItems[foodMatID] < foodMatNum) 
				{
					match = false;
					break;
				}
			}
			if (match)
			{
				//记录食材最少和最短时间的作为当前菜单
				int numOfItems = menuData.GetNumOfItems();
				if (numOfItems < minCookingMateiralCount)
				{
					//if (menuData.CookingTime < minCookingTime)
					{
						minCookingMateiralCount = numOfItems;
						minCookingTime = menuData.CookingTime;
						matchMenuData = menuData;
						if (!canFind)
							canFind = true;
					}
				}
			}
		}
	}
	return canFind;
}
bool ACookingMachine::CanAddFoodMateral(int32 FoodMaterialID)
{
	if (OnFire || Repairing)
		return false;

	FMenuData menuData;
	if (CheckMenu(FoodMaterialID,menuData))
	{
	//	FoodMaterial.Add(FoodMaterialID, 0);
		return true;
	}
	return false;
	// if (TargetMenuData.FoodID == -2) //当前没有烹饪任何菜单，先找匹配的菜单
	// {
	// 	
	// 	return false;
	// }
	// else //当前已经有烹饪菜单，找菜单里面匹配的
	// {
	// 	auto menueNeed = TargetMenuData.MenuItems.Find(FoodMaterialID);
	// 	if (menueNeed != nullptr) //是否菜单的食材
	// 	{
	// 		const int needCount = *menueNeed;
	// 		const auto fooldMat = FoodMaterial.Find(FoodMaterialID);
	// 		if (fooldMat == nullptr) //没持有该食材
	// 		{
	// 			FoodMaterial.Add(FoodMaterialID, 0);
	// 			return true;
	// 		}
	// 		else if (FoodMaterial[FoodMaterialID] < needCount) //食材未满
	// 		{
	// 			return true;
	// 		}
	// 	}
	// 	return false;
	// }
}

int ACookingMachine::GetMenuFood()
{
	if (IsFinish)
	{
		int foodID = TargetMenuData.FoodID;
		OnGetFood();
		ResetFoodData();
		return  foodID;
	}
	return  -1;
}

void ACookingMachine::ClearCookedStack()
{
	CookedActorStack.Empty();
}

float ACookingMachine::GetCookSpeedMultiplier() const
{
	float Bonus = 0.f;
	if (const UWorld* W = GetWorld())
	{
		if (const UBaseGameInstance* GI = Cast<UBaseGameInstance>(W->GetGameInstance()))
		{
			if (GI->economySystem)
			{
				Bonus = GI->economySystem->GetOwnedItemCount(EconomyShopItemIDs::AdvancedStove) * CookSpeedBonusPerStove;
			}
		}
	}
	return FMath::Clamp(1.f - Bonus, MinCookSpeedMultiplier, 1.f);
}

float ACookingMachine::GetRequiredCookTime() const
{
	return TargetMenuData.CookingTime * GetCookSpeedMultiplier();
}

/** 取 Actor 的世界包围盒高度（Extent 为半高，乘 2 得到实际高度） */
static float GetActorHeight(AActor* InActor)
{
	FVector Origin, Extent;
	InActor->GetActorBounds(false, Origin, Extent);
	return Extent.Z * 2.0f;
}

void ACookingMachine::SpawnCookedActor()
{
	// 开关未开启则不生成
	if (!bSpawnOnCookingFinish) return;

	// 清理已被销毁/拾取走的成品记录
	for (int32 i = CookedActorStack.Num() - 1; i >= 0; --i)
	{
		if (!CookedActorStack[i].Get())
		{
			CookedActorStack.RemoveAt(i);
		}
	}

	// 成品类以菜单数据（FMenuData，DT_Foods）为准：
	// 如番茄+鸡蛋 → 匹配到"番茄炒鸡蛋"菜品，取其 CookedActorClass 生成 1 个成品
	// 菜品行未配置成品类时，用机器上的默认类兜底
	TSubclassOf<AActor> ClassToSpawn = TargetMenuData.CookedActorClass ? TargetMenuData.CookedActorClass : CookedActorClass;

	// 没有可生成的成品类，走原始逻辑（保持完成状态，等待玩家取餐）
	if (!ClassToSpawn) return;

	// 累计当前已叠放成品的总高度
	float StackHeight = 0.f;
	for (const TObjectPtr<AActor>& Stacked : CookedActorStack)
	{
		StackHeight += GetActorHeight(Stacked);
	}

	// 在指定位置向上偏移叠高（未摆放 SpawnPoint 时使用机器自身位置）
	const FVector BaseLocation = SpawnPoint ? SpawnPoint->GetComponentLocation() : GetActorLocation();
	const FRotator SpawnRotation = SpawnPoint ? SpawnPoint->GetComponentRotation() : GetActorRotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParams.Owner = this;

	// 达到叠高上限：不再走自动生成成品逻辑，直接走原始逻辑
	// （机器保持"完成"状态，等待玩家通过取餐 GetMenuFood 拿菜）
	if (MaxStackCount > 0 && CookedActorStack.Num() >= MaxStackCount)
	{
		UE_LOG(LogTemp, Log, TEXT("[CookingMachine] 成品已叠满 %d 层，本次不生成，等待玩家取餐"), CookedActorStack.Num());
		return;
	}

	const FVector SpawnLocation = BaseLocation + FVector(0.f, 0.f, StackHeight + StackSpacing * CookedActorStack.Num());
	AActor* CookedActor = GetWorld()->SpawnActor<AActor>(ClassToSpawn, SpawnLocation, SpawnRotation, SpawnParams);
	if (!CookedActor) return; // 生成失败，走原始逻辑

	CookedActorStack.Add(CookedActor); // 记入堆栈，下一个成品会叠在它上面
	UE_LOG(LogTemp, Log, TEXT("[CookingMachine] 烹饪完成，已生成成品: %s，当前叠放层数: %d"), *CookedActor->GetName(), CookedActorStack.Num());

	// 把成品对应的菜单菜品 ID 写到物品上，供上菜台校验“是否在菜单中”
	if (AItemActor* Item = Cast<AItemActor>(CookedActor))
	{
		Item->SetMenuFoodID(TargetMenuData.FoodID);

		// 成品品质 = 所有入锅食材中的最差品质（任一食材为次品则成品为次品；全为佳品才为佳品）
		EIngredientQuality DishQuality = EIngredientQuality::Premium;
		for (AItemActor* FoodActor : FoodActors)
		{
			if (FoodActor)
			{
				DishQuality = MinQuality(DishQuality, FoodActor->GetIngredientQuality());
			}
		}
		Item->SetIngredientQuality(DishQuality);

		// 启用成品腐烂：未被玩家拾取的成品会自动销毁（放下后重新计时）
		if (bProductsDecay)
		{
			Item->EnableDecay(ProductDecayTime);
		}
	}

	// 已生成成品：删除锅里的食材，并将锅的状态恢复成初始状态
	for (AItemActor* FoodActor : FoodActors)
	{
		if (FoodActor)
		{
			FoodActor->Destroy();
		}
	}
	FoodActors.Empty();
	ResetFoodData();
}

bool ACookingMachine::AddFoodMateral(int32 FoodMaterialID)
{
	bool canAdd = CanAddFoodMateral(FoodMaterialID);
	if (canAdd)
	{
		FMenuData tmpMenu;
		CheckMenu(FoodMaterialID, tmpMenu);
		if (tmpMenu.FoodID != TargetMenuData.FoodID) //转换目标菜单
		{
			TargetMenuData = tmpMenu;
			TargetMateiralCount = TargetMenuData.GetNumOfItems();
		}
		if (!FoodMaterial.Contains(FoodMaterialID))
			FoodMaterial.Add(FoodMaterialID,1);
		else
			FoodMaterial[FoodMaterialID] += 1;

		CurFoodCount = 0;
		for (auto fmat : FoodMaterial)
		{
			CurFoodCount += fmat.Value;
		}
		GreenCookTime = (CurFoodCount * GetRequiredCookTime() / TargetMateiralCount) ; //更新煮糊时间（含灶台加速）
		OverCookTime = 0;
		if (CurFoodCount == TargetMateiralCount)
		{
			IsFull = true;
		}
		OnAddFoodMateiral();//添加食材回调
		if (!IsCooking  && tmpMenu.CookingTime>0.0f)
		{
			IsCooking = true;
			CurCookingTime = 0;
			OnStartCookingEvent();//开始烹饪
		}
		//这里新添加的食材可能会让烹饪时间变为0，直接完成烹饪，用于测试直接上屎菜
		if (tmpMenu.CookingTime==0.0f)
		{
			IsCooking = false;
			IsFinish = true;
			OnCookingFinishEvent();//完成烹饪
			SpawnCookedActor();//尝试自动生成成品（叠满或开关关闭时走原始逻辑：保持完成状态，等待玩家取餐）
		}
	}
	return canAdd;
}

// Called every frame
void ACookingMachine::Tick(float DeltaTime)
{
	if (OnFire && Repairing)//维修中
	{
		CurRepairTime += DeltaTime;
		if (CurRepairTime >= TargetRepairTime)//完成维修
		{
			ResetFoodData();
			OnRepairFinish();
		}
	}
	else
	{
		if (IsCooking)
		{
			float NewCookingTime = CurCookingTime + DeltaTime;
			if (NewCookingTime > GetRequiredCookTime() )
			{
				OverCookTime += DeltaTime;
			}
			else
			{
				CurCookingTime = NewCookingTime;
			}
			if (OverCookTime >= MaxOverCookTime && TargetMenuData.CanOverCook && !OnFire) //超时
			{
				OnFire = true;
				IsCooking = false;
				IsFinish = false;
				CurRepairTime = 0;
				CurCookingTime = 0;
				OverCookTime = 0;
				OnOverCookEvent();
			}
			if ((CurCookingTime + OverCookTime) >= GetRequiredCookTime() && !IsFinish && IsFull) //熟了（含灶台加速）
			{
				IsFinish = true;
				if (!TargetMenuData.CanOverCook)
				{
					IsCooking =  false;
				}
				OnCookingFinishEvent();
				SpawnCookedActor();//尝试自动生成成品（叠满或开关关闭时走原始逻辑：保持完成状态，等待玩家取餐）
			}
		}
	}
	Super::Tick(DeltaTime);

}

void ACookingMachine::OnBoxOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	//FString ActorNamel = OtherActor->GetName();
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Actor名称: %s"), *ActorNamel) );
	IPickupInterface* PickupInterface = Cast<IPickupInterface>(OtherActor);
	if (PickupInterface)
	{
		PickupInterface->SetCookMachineItem(this);
		
		//当tips没有被其他玩家占用的时候
		if(LocalPlayer == nullptr)
		{
			TipsWidget->SetVisibility(true);
			if (APawn* AP = Cast<APawn>(OtherActor))
			{
				if (APlayerController* APC = Cast<APlayerController>(AP->GetController()))
				{
					// 从玩家控制器中拿到本地玩家对象
					if (ULocalPlayer* Player = APC->GetLocalPlayer())
					{
						// 为这个组件设置OwnerPlayer，这样引擎就知道它属于谁了
						TipsWidget->SetOwnerPlayer(Player);
						LocalPlayer = Player;
					}
				}
			}
		}
	}
	
}

void ACookingMachine::OnBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	IPickupInterface* PickupInterface = Cast<IPickupInterface>(OtherActor);
	if (PickupInterface)
	{
		PickupInterface->SetCookMachineItem(nullptr);
		if(LocalPlayer)
		{
			if (APawn* AP = Cast<APawn>(OtherActor))
			{
				if (APlayerController* APC = Cast<APlayerController>(AP->GetController()))
				{
					// 从玩家控制器中拿到本地玩家对象
					if (ULocalPlayer* Player = APC->GetLocalPlayer())
					{
						// 为这个组件设置OwnerPlayer，这样引擎就知道它属于谁了
						if(LocalPlayer == Player) 
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


