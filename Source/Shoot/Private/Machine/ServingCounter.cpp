// Fill out your copyright notice in the Description page of Project Settings.

#include "Machine/ServingCounter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Item/ItemActor.h"
#include "ShootCharacter.h"
#include "NPC/GuestCharacter.h"
#include "NPC/ChairSeat.h"
#include "Engine/Engine.h"
#include "Game/BaseGameInstance.h"
#include "Game/EconomySystem.h"
#include "Kismet/GameplayStatics.h"

AServingCounter::AServingCounter()
{
	PrimaryActorTick.bCanEverTick = false;

	// 外观模型作为根组件，交互球挂在其上
	CounterMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CounterMesh"));
	RootComponent = CounterMesh;

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(RootComponent);
	InteractionSphere->SetSphereRadius(140.f);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	InteractionSphere->SetGenerateOverlapEvents(true); // 必须显式开启，否则不会触发 OnComponentBeginOverlap
}

void AServingCounter::BeginPlay()
{
	Super::BeginPlay();

	InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &AServingCounter::OnInteractionOverlap);
	InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &AServingCounter::OnInteractionEndOverlap);
}

bool AServingCounter::IsFoodInMenu(int32 FoodID) const
{
	FMenuData Dummy;
	return GetMenuDataForFood(FoodID, Dummy);
}

bool AServingCounter::GetMenuDataForFood(int32 FoodID, FMenuData& OutMenuData) const
{
	if (!MenuDataTable || FoodID < 0)
	{
		return false;
	}

	const TMap<FName, uint8*>& RowMap = MenuDataTable->GetRowMap();
	for (const TPair<FName, uint8*>& Pair : RowMap)
	{
		if (const FMenuData* Row = MenuDataTable->FindRow<FMenuData>(Pair.Key, TEXT("ServingCounter::GetMenuDataForFood")))
		{
			// 行名约定为 FoodID：行名或行内字段命中均可（两者不一致时以行名为准）
			if (Row->ResolveFoodID(Pair.Key) == FoodID)
			{
				OutMenuData = *Row;
				OutMenuData.FoodID = FoodID;
				return true;
			}
		}
	}
	return false;
}

void AServingCounter::ApplyQualityReward(AItemActor* Dish)
{
	if (!Dish)
	{
		return;
	}

	// 普通品质不额外结算（FMenuData 中未配置奖励时为 0，也已跳过）
	const EIngredientQuality Quality = Dish->GetIngredientQuality();
	if (Quality == EIngredientQuality::Normal)
	{
		return;
	}

	FMenuData MenuData;
	if (!GetMenuDataForFood(Dish->GetMenuFoodID(), MenuData))
	{
		return;
	}

	int32 MoneyBonus = 0;
	int32 ReputationBonus = 0;
	if (Quality == EIngredientQuality::Premium)
	{
		MoneyBonus = MenuData.PremiumBonusMoney;
		ReputationBonus = MenuData.PremiumBonusReputation;
	}
	else if (Quality == EIngredientQuality::Inferior)
	{
		MoneyBonus = MenuData.InferiorBonusMoney;
		ReputationBonus = MenuData.InferiorBonusReputation;
	}

	if (MoneyBonus == 0 && ReputationBonus == 0)
	{
		return;
	}

	if (UWorld* W = GetWorld())
	{
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(W->GetGameInstance()))
		{
			if (GI->economySystem)
			{
				if (MoneyBonus != 0)
				{
					GI->economySystem->AddMoney(MoneyBonus, EMoneySource::OrderComplete);
				}
				if (ReputationBonus != 0)
				{
					GI->economySystem->AddReputation(ReputationBonus);
				}
				UE_LOG(LogTemp, Log, TEXT("[ServingCounter] 品质奖励（%d）金钱%+d 声誉%+d"),
					static_cast<int32>(Quality), MoneyBonus, ReputationBonus);
			}
		}
	}
}

AGuestCharacter* AServingCounter::FindWaitingGuest() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TArray<AActor*> Guests;
	UGameplayStatics::GetAllActorsOfClass(World, AGuestCharacter::StaticClass(), Guests);
	for (AActor* Actor : Guests)
	{
		if (AGuestCharacter* Guest = Cast<AGuestCharacter>(Actor))
		{
			if (Guest->GuestState == EGuestState::WaitingFood)
			{
				return Guest;
			}
		}
	}
	return nullptr;
}

bool AServingCounter::SubmitDish(AItemActor* Dish)
{
	if (!Dish)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("[上菜台] 没有可提交的菜品"));
		return false;
	}

	const int32 FoodID = Dish->GetMenuFoodID();

	if (bUseMenuControll)
	{
		TArray<int> FoodIdArray ;
		FoodIdArray.Add(FoodID);

		bool success = FoodID != -1 && Cast<UBaseGameInstance>(GetGameInstance())->curLevelBP->orderSystem->PushFoods(FoodIdArray);
		if (success)
		{
			// 已在上方通过 orderSystem->PushFoods 完成订单（驱动分数/声誉/金钱）
			UE_LOG(LogTemp, Log, TEXT("[ServingCounter] 上菜成功！菜品(FoodID=%d)已送达客人"), FoodID);
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
				FString::Printf(TEXT("[上菜台] 上菜成功！FoodID=%d"), FoodID));
			// 先按成品品质结算额外奖励（佳品/次品）
			ApplyQualityReward(Dish);
			// 上菜到客人位置（读取本次 PushFoods 实际服务的客人），停留后销毁
			AGuestCharacter* ServedGuest = Cast<UBaseGameInstance>(GetGameInstance())->curLevelBP->orderSystem->GetLastServedGuest();
			DeliverDishToSeat(Dish, ServedGuest);
			OnServeSuccess(FoodID);
			return true;
		}
		else
		{
    	
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red
				, FString::Printf(TEXT("上菜失败%i"), FoodID));
			return false;
		}
	}
	else
	{
		
		UE_LOG(LogTemp, Log, TEXT("[ServingCounter] 提交菜品 class=%s FoodID=%d"), *Dish->GetClass()->GetName(), FoodID);
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan,
			FString::Printf(TEXT("[上菜台] 提交菜品 FoodID=%d"), FoodID));
		
		// 0) 菜单数据表是否配置
		if (!MenuDataTable)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ServingCounter] 上菜失败：未配置 MenuDataTable"));
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("[上菜台] 未配置菜单数据表(MenuDataTable)"));
			OnServeFail(FoodID, TEXT("NoMenuTable"));
			return false;
		}
		
		// 1) 校验是否在菜单中
		if (!IsFoodInMenu(FoodID))
		{
			UE_LOG(LogTemp, Log, TEXT("[ServingCounter] 上菜失败：菜品(FoodID=%d)不在菜单中"), FoodID);
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red,
				FString::Printf(TEXT("[上菜台] 上菜失败：菜品(FoodID=%d)不在菜单中"), FoodID));
			OnServeFail(FoodID, TEXT("NotInMenu"));
			return false;
		}
		
		// 2) 找一个正在等餐的客人
		AGuestCharacter* TargetGuest = nullptr;
		if (bRequireWaitingGuest)
		{
			TargetGuest = FindWaitingGuest();
			if (!TargetGuest)
			{
				UE_LOG(LogTemp, Log, TEXT("[ServingCounter] 上菜失败：当前没有正在等餐的客人 (FoodID=%d)"), FoodID);
				GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red,
					TEXT("[上菜台] 上菜失败：当前没有正在等餐的客人"));
				OnServeFail(FoodID, TEXT("NoWaitingGuest"));
				return false;
			}
		
			TargetGuest->StartEating();
		}
		
		// 3) 成功：消费（销毁）成品，并同步完成订单系统里的对应订单（驱动分数、声誉、金钱）
		UE_LOG(LogTemp, Log, TEXT("[ServingCounter] 上菜成功！菜品(FoodID=%d)已送达客人"), FoodID);
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
			FString::Printf(TEXT("[上菜台] 上菜成功！FoodID=%d"), FoodID));

		// 先按成品品质结算额外奖励（佳品/次品），随后把成品送到客人位置（停留后销毁）
		ApplyQualityReward(Dish);

		// 取出碟子里的菜品 ID（拷贝一份，避免 Destroy 后悬空引用）
		const TArray<int32> DishFoods = Dish->FoodIdArray;
		DeliverDishToSeat(Dish, TargetGuest);

		if (UWorld* W = GetWorld())
		{
			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(W->GetGameInstance()))
			{
				if (ABaseLevelBP* Lvl = GI->curLevelBP)
				{
					if (AOrderSystem* OS = Lvl->orderSystem)
					{
						// 优先用碟子实际菜品列表，为空则退回单个 FoodID
						if (DishFoods.Num() > 0)
						{
							OS->PushFoods(DishFoods);
						}
						else if (FoodID != -1)
						{
							OS->PushFoods({ FoodID });
						}
					}
				}
			}
		}

		OnServeSuccess(FoodID);
		return true;
	}

}

void AServingCounter::DeliverDishToSeat(AItemActor* Dish, AGuestCharacter* Guest)
{
	if (!Dish)
	{
		return;
	}
	// 无目标客人（或开关关闭）：退回旧行为，直接销毁
	if (!Guest || !bDeliverToSeat)
	{
		Dish->Destroy();
		return;
	}

	// 客人有座位餐桌：把成品摆到座位的碟子挂点上（位置由该餐桌蓝图里的 PlateSocket 调整），
	// 并交给客人记录 —— 客人吃完后由座位把它转成空碟，等玩家收走才释放座位。
	if (Guest->SeatActor && Guest->SeatActor->PlacePlateOnSocket(Dish))
	{
		Guest->PlateOnTable = Dish;
		// 订单已结算：同步清掉座位上的菜单绑定
		Guest->SeatActor->ClearOrder();
		return;
	}

	// 客人没有座位：旧行为，送到客人位置停留后销毁
	// 把成品送到客人面前（座位处略抬高），停用碰撞，停留 ServeArriveDelay 后销毁，视觉上“上菜到客人位置”
	const FVector Dest = Guest->GetActorLocation() + FVector(0.f, 0.f, 30.f);
	Dish->SetActorLocation(Dest);
	Dish->SetActorEnableCollision(false);

	if (UWorld* W = GetWorld())
	{
		const TWeakObjectPtr<AItemActor> WeakDish(Dish);
		const float Delay = FMath::Max(0.1f, ServeArriveDelay);
		FTimerHandle TH;
		W->GetTimerManager().SetTimer(TH, FTimerDelegate::CreateLambda([WeakDish]()
		{
			if (WeakDish.IsValid())
			{
				WeakDish->Destroy();
			}
		}), Delay, false);
	}
}

void AServingCounter::OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		Character->SetOverlappingCounter(this);
		UE_LOG(LogTemp, Log, TEXT("[ServingCounter] 玩家进入范围，注册 OverlappingCounter"));
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow, TEXT("[上菜台] 已进入范围"));
	}
}

void AServingCounter::OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		// 仅当仍然指向本上菜台时才清空（避免多人/多台串扰）
		if (Character->GetOverlappingCounter() == this)
		{
			Character->SetOverlappingCounter(nullptr);
		}
	}
}
