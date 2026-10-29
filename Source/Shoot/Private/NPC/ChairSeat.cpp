// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/ChairSeat.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/WidgetComponent.h"
#include "UI/ChairTipsWidget.h"
#include "NPC/GuestCharacter.h"
#include "Item/ItemActor.h"
#include "ShootCharacter.h"
#include "Game/BaseGameInstance.h"
#include "Game/BaseLevelBP.h"
#include "Game/OrderSystem.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"

namespace
{
	/** 餐桌交互统一提示：屏幕消息 + Output Log（失败原因默认可见，便于排查） */
	void SeatMessage(const FString& Msg, const FColor& Color, float Duration = 3.f)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, Duration, Color, Msg);
		}
		UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	}

	/** 菜品 ID 列表转成 "1,2,3" 形式，用于提示客人点的是什么、手上拿的是什么 */
	FString JoinFoodIDs(const TArray<int32>& FoodIDs)
	{
		FString Out;
		for (int32 i = 0; i < FoodIDs.Num(); i++)
		{
			if (i > 0)
			{
				Out += TEXT(",");
			}
			Out += FString::FromInt(FoodIDs[i]);
		}
		if (Out.IsEmpty())
		{
			Out = TEXT("空");
		}
		return Out;
	}
}

AChairSeat::AChairSeat()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	SeatMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SeatMesh"));
	SeatMesh->SetupAttachment(RootComponent);

	// 碟子挂点：默认在凳面上方，实际摆放位置由蓝图视口拖动调整
	PlateSocket = CreateDefaultSubobject<USceneComponent>(TEXT("PlateSocket"));
	PlateSocket->SetupAttachment(RootComponent);
	PlateSocket->SetRelativeLocation(FVector(0.f, 0.f, 60.f));

	// 入座挂点：客人坐下的位置与朝向全由它决定（默认在凳子上方 50cm，与旧版 SitSocketOffset 一致），
	// 在蓝图视口 / 关卡实例里选中 SitSocket 拖动即可调整，不再需要代码偏移量
	SitSocket = CreateDefaultSubobject<USceneComponent>(TEXT("SitSocket"));
	SitSocket->SetupAttachment(RootComponent);
	SitSocket->SetRelativeLocation(FVector(0.f, 0.f, 50.f));

	// 交互球：玩家靠近后按 E 与本餐桌交互（点单 / 上菜 / 收空碟）
	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(RootComponent);
	InteractionSphere->SetSphereRadius(InteractionRadius);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	InteractionSphere->SetGenerateOverlapEvents(true);

	// 菜单提示控件：挂在餐桌上方，显示当前客人点的菜单。
	// 控件类由蓝图在 TipsWidgetComp 的 Widget Class 上配置，这里不给默认类，
	// 免得蓝图还没配就被一个空控件占位。位置/尺寸同样可在蓝图视口里直接拖动调整。
	TipsWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("TipsWidgetComp"));
	TipsWidgetComp->SetupAttachment(RootComponent);
	TipsWidgetComp->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	TipsWidgetComp->SetWidgetSpace(EWidgetSpace::Screen);
	TipsWidgetComp->SetDrawAtDesiredSize(true);
	TipsWidgetComp->SetTwoSided(true);
	TipsWidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TipsWidgetComp->SetGenerateOverlapEvents(false);
	TipsWidgetComp->SetVisibility(false);
}

void AChairSeat::BeginPlay()
{
	Super::BeginPlay();

	if (InteractionSphere)
	{
		InteractionSphere->SetSphereRadius(InteractionRadius);
		InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &AChairSeat::OnInteractionOverlap);
		InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &AChairSeat::OnInteractionEndOverlap);
	}

	// 先把控件建出来（供蓝图 OnOwnerSeatSet 做初始化），再按当前桌况决定显隐
	RefreshTipsWidget();
}

// ==================== 座位 ====================

bool AChairSeat::CanOccupy() const
{
	// 有客人坐着，或桌上有未回收的碟子（成品 / 空碟）时，下一位客人不能坐
	return !bIsOccupied && !HasPlateOnSeat();
}

bool AChairSeat::Occupy(AGuestCharacter* Guest)
{
	if (!Guest)
	{
		return false;
	}
	// 重复占用同一座位（SitDown 会再调一次）：视为成功
	if (Occupant == Guest)
	{
		return true;
	}
	if (!CanOccupy())
	{
		return false;
	}
	bIsOccupied = true;
	Occupant = Guest;
	RefreshTipsWidget();
	return true;
}

void AChairSeat::Release()
{
	// 客人离座时先把菜单绑定清掉（未完成的订单由客人自己向订单系统取消）
	ClearOrder();
	Occupant = nullptr;

	// 桌上还摆着一道没吃过的成品（客人被强制清场 / 异常销毁）：转成空碟交给玩家收走，
	// 避免座位被一道永远收不走的菜永久占住。
	if (IsValid(PlateOnSeat) && !PlateOnSeat->bIsEmptyPlate)
	{
		SetDirtyPlate(PlateOnSeat);
		RefreshTipsWidget();
		return;
	}

	// 空碟未回收：座位保持占用，等玩家按 E 收走空碟由 ClearDirtyPlate 释放
	if (HasPlateOnSeat() || bHasDirtyPlate)
	{
		bIsOccupied = true;
		RefreshTipsWidget();
		return;
	}

	bIsOccupied = false;
	DirtyPlate = nullptr;
	bHasDirtyPlate = false;
	RefreshTipsWidget();
}

void AChairSeat::ForceRelease()
{
	// 关卡收尾 / 清场用：无论桌上还有什么，都直接释放座位并清掉残留碟子
	ClearOrder();
	Occupant = nullptr;

	if (IsValid(PlateOnSeat))
	{
		PlateOnSeat->Destroy();
	}
	PlateOnSeat = nullptr;
	DirtyPlate = nullptr;
	bHasDirtyPlate = false;
	bIsOccupied = false;
	RefreshTipsWidget();
}

FTransform AChairSeat::GetSitTransform() const
{
	// 客人入座的位置 / 朝向完全以 SitSocket 挂点为准（无论在蓝图还是关卡实例里拖动都生效）；
	// 挂点缺失（异常情况）时退回座位 Actor 自身的变换，避免返回无效变换把客人送到原点。
	if (SitSocket)
	{
		// 只取挂点的位置与旋转：椅子整体可能被缩放，直接套用会把客人拉长，缩放固定为 1
		FTransform SitTransform = SitSocket->GetComponentTransform();
		SitTransform.SetScale3D(FVector::OneVector);
		return SitTransform;
	}
	return GetActorTransform();
}

// ==================== 订单（菜单绑定） ====================

void AChairSeat::BindOrder(UOrderHandle* Order)
{
	BoundOrder = Order;
	BoundFoodIDs.Reset();
	BoundSetName.Reset();

	if (Order)
	{
		// 订单数据按值拷贝，蓝图不用解析订单对象也能显示菜单
		BoundFoodIDs = Order->GetOrderData().FoodIDs;
		// 展示名由菜品表（FMenuData::FoodName）拼接而来，订单里不再单独存 SetName
		BoundSetName = Order->GetOrderDisplayName();

		SeatMessage(FString::Printf(TEXT("[餐桌] %s 已绑定订单#%d【%s】菜品[%s]"),
			*GetName(), Order->orderId, *BoundSetName, *JoinFoodIDs(BoundFoodIDs)), FColor::Cyan);
	}

	OnOrderBound(Order ? Order->orderId : -1, BoundFoodIDs);

	// 菜单已绑定：让桌面提示控件立刻显示本次点单内容
	RefreshTipsWidget();

	// 通知监听菜单的 UI（菜单 ID 列表已就绪，可直接拿去查图标）
	OnMenuChanged.Broadcast(BoundFoodIDs);
}

void AChairSeat::ClearOrder()
{
	// 记下清空前是否真的有菜单，避免空广播刷 UI
	const bool bHadMenu = (BoundOrder != nullptr) || (BoundFoodIDs.Num() > 0);

	BoundOrder = nullptr;
	BoundFoodIDs.Reset();
	BoundSetName.Reset();
	RefreshTipsWidget();

	if (bHadMenu)
	{
		// 传出空数组：UI 据此清掉菜单 / ICON 显示
		OnMenuChanged.Broadcast(BoundFoodIDs);
	}
}

TArray<int32> AChairSeat::GetMenuFoodIDs() const
{
	return BoundFoodIDs;
}

FString AChairSeat::GetMenuSetName() const
{
	if (!BoundSetName.IsEmpty())
	{
		return BoundSetName;
	}

	// 兜底：直接从绑定的订单取（如蓝图手动绑了订单但没走 BindOrder）
	return BoundOrder ? BoundOrder->GetOrderDisplayName() : FString();
}

bool AChairSeat::AreFoodIDsMatching(const TArray<int32>& A, const TArray<int32>& B)
{
	if (A.Num() != B.Num())
	{
		return false;
	}

	// 统计出现次数逐项抵扣：与订单系统 PushFoods/PushFoodsForGuest 的匹配规则保持一致
	TMap<int32, int32> NeedCount;
	for (int32 ID : B)
	{
		NeedCount.FindOrAdd(ID)++;
	}
	for (int32 ID : A)
	{
		int32* Count = NeedCount.Find(ID);
		if (!Count || *Count <= 0)
		{
			return false;
		}
		(*Count)--;
	}
	return true;
}

TArray<int32> AChairSeat::GetDishFoodIDs(AItemActor* Dish)
{
	TArray<int32> FoodIDs;
	if (!Dish)
	{
		return FoodIDs;
	}

	// 优先用碟子里的真实菜品列表，为空则退回单个菜品 ID（与旧上菜逻辑一致）
	FoodIDs = Dish->FoodIdArray;
	if (FoodIDs.Num() == 0)
	{
		const int32 FoodID = Dish->GetMenuFoodID();
		if (FoodID >= 0)
		{
			FoodIDs.Add(FoodID);
		}
	}
	return FoodIDs;
}

// ==================== 碟子 / 空碟 ====================

bool AChairSeat::HasPlateOnSeat() const
{
	return IsValid(PlateOnSeat);
}

FTransform AChairSeat::GetPlateSocketTransform() const
{
	return PlateSocket ? PlateSocket->GetComponentTransform() : GetActorTransform();
}

bool AChairSeat::PlacePlateOnSocket(AItemActor* Plate)
{
	if (!Plate || !PlateSocket)
	{
		return false;
	}

	// 先把物品网格从手上 / 其它挂点摘下，再把整个碟子吸附到本座位的挂点：
	// 之后碟子跟随餐桌移动，摆放位置与朝向完全由蓝图里的 PlateSocket 决定。
	Plate->DeAttachMeshToSocket();
	Plate->AttachToComponent(PlateSocket, FAttachmentTransformRules::SnapToTargetNotIncludingScale);

	// 桌上的碟子不参与拾取与腐烂（空碟走 CollectSphere 的回收交互，见 SetDirtyPlate）
	Plate->bPickable = false;
	Plate->bEnableDecay = false;
	if (Plate->Sphere)
	{
		Plate->Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Plate->Sphere->SetGenerateOverlapEvents(false);
	}
	Plate->SetItemState(EItemState::EIS_Idle);

	PlateOnSeat = Plate;
	return true;
}

void AChairSeat::SetDirtyPlate(AItemActor* Plate)
{
	if (!IsValid(Plate))
	{
		return;
	}

	// 客人已离座但空碟暂占座位：保持 bIsOccupied 为真，避免新客人抢占，
	// 直到玩家按 E 回收空碟后 ClearDirtyPlate 才真正释放。
	bIsOccupied = true;
	Occupant = nullptr;
	ClearOrder();

	Plate->bIsEmptyPlate = true;
	Plate->bIsDirtyPlate = false;
	Plate->bPickable = false;
	Plate->OwningSeat = this;

	// 清掉碟上的食材，视觉上变成真正的“空碟”（只留盘子的网格）
	Plate->OnClear();

	// 空碟同样摆在挂点上，位置由蓝图里的 PlateSocket 调整
	PlacePlateOnSocket(Plate);

	// 启用回收球：玩家靠近即可按 E 收走空碟
	if (Plate->CollectSphere)
	{
		Plate->CollectSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Plate->CollectSphere->SetGenerateOverlapEvents(true);
	}

	DirtyPlate = Plate;
	bHasDirtyPlate = true;
	RefreshTipsWidget();
}

void AChairSeat::ClearDirtyPlate()
{
	bIsOccupied = false;
	Occupant = nullptr;
	DirtyPlate = nullptr;
	bHasDirtyPlate = false;
	PlateOnSeat = nullptr;
	ClearOrder();

	// 真正释放座位：通知客人管理器安排下一位客人入座
	OnSeatFreedDelegate.Broadcast();
	RefreshTipsWidget();
}

// ==================== 菜单提示控件（TipsWidget） ====================

EChairTipsState AChairSeat::GetSeatState() const
{
	// 桌上有空碟：客人已经走了，只剩待回收的碟子（优先级最高）
	if (IsValid(PlateOnSeat) && PlateOnSeat->bIsEmptyPlate)
	{
		return EChairTipsState::DirtyPlate;
	}

	// 已绑定菜单：等待玩家上菜
	if (BoundOrder)
	{
		return EChairTipsState::WaitingFood;
	}

	if (IsValid(Occupant))
	{
		switch (Occupant->GuestState)
		{
		case EGuestState::WaitingOrder:
			return EChairTipsState::WaitingOrder;
		case EGuestState::WaitingFood:
			return EChairTipsState::WaitingFood;
		case EGuestState::Eating:
			return EChairTipsState::Eating;
		default:
			// 走向座位 / 入座动画等过渡状态
			return EChairTipsState::Seated;
		}
	}
	return EChairTipsState::Empty;
}

UChairTipsWidget* AChairSeat::GetTipsWidget() const
{
	if (!TipsWidgetComp)
	{
		return nullptr;
	}
	// 控件实例可能尚未创建（组件隐藏时不会创建），此时返回 nullptr
	return Cast<UChairTipsWidget>(TipsWidgetComp->GetUserWidgetObject());
}

void AChairSeat::RefreshTipsWidget()
{
	if (!TipsWidgetComp)
	{
		return;
	}

	// 先决定显隐（显示时会把控件实例建出来），再把桌况与菜单推给控件
	UpdateTipsVisibility();

	if (UChairTipsWidget* Tips = GetTipsWidget())
	{
		const bool bWasNull = (Tips->OwnerSeat == nullptr);
		Tips->OwnerSeat = this;
		// 仅在第一次赋值时通知蓝图，避免重复绑出多个处理函数
		if (bWasNull)
		{
			Tips->OnOwnerSeatSet();
		}
		Tips->RefreshTips();
	}
}

void AChairSeat::UpdateTipsVisibility()
{
	if (!TipsWidgetComp)
	{
		return;
	}

	bool bShow = false;
	switch (TipsMode)
	{
	case EChairSeatTipsMode::Always:
		// 有人 / 有菜单 / 有碟子时常显
		bShow = HasOrder() || IsValid(Occupant) || bIsOccupied || HasPlateOnSeat();
		break;
	case EChairSeatTipsMode::PlayerNear:
		// 与机器类 TipsWidget 一致：玩家走进交互范围才显示
		bShow = TipsNearbyPlayerCount > 0;
		break;
	case EChairSeatTipsMode::OrderOnly:
	default:
		// 默认：只有绑定了菜单（客人已点单、等待上菜）才显示
		bShow = HasOrder();
		break;
	}

	if (bShow)
	{
		// 组件隐藏时不会创建控件实例，显示前先确保建出来（InitWidget 内部幂等）
		TipsWidgetComp->InitWidget();
	}
	if (TipsWidgetComp->IsVisible() != bShow)
	{
		TipsWidgetComp->SetVisibility(bShow);
	}
}

// ==================== 玩家交互（E 键） ====================

bool AChairSeat::TryInteractByPlayer(AShootCharacter* Player)
{
	if (!Player)
	{
		return false;
	}

	// 1) 手上有物品：上菜（脏碟例外，脏碟只能拿去洗碗机交还）
	if (AItemActor* Held = Player->GetPickingItem())
	{
		// 空座位（没客人也没碟子）不与手上的东西互动：不拦截，交给后续拾取/做菜等逻辑
		if (!IsValid(Occupant) && !HasPlateOnSeat())
		{
			return false;
		}

		if (Held->bIsDirtyPlate)
		{
			SeatMessage(TEXT("[餐桌] 手上是脏碟，请走到洗碗机旁按 E 交还清洗"), FColor::Yellow);
			return true;
		}
		TryServeDishByPlayer(Player, Held);
		return true;
	}

	// 2) 空手 + 桌上有空碟：收走空碟（座位此时才真正释放，下一位客人才能入座）
	if (CanPlayerCollectPlate(Player))
	{
		TryCollectPlateByPlayer(Player);
		return true;
	}

	// 3) 空手 + 客人已坐下等待点单：给客人下单，并把菜单绑定到本餐桌
	if (CanPlayerTakeOrder(Player))
	{
		TryTakeOrderByPlayer(Player);
		return true;
	}

	// 4) 其它状态：只给提示，不拦截后续交互逻辑
	if (IsValid(Occupant) && Occupant->GuestState == EGuestState::WaitingFood)
	{
		SeatMessage(TEXT("[餐桌] 客人已点单，请把做好的菜拿过来按 E 上菜"), FColor::Cyan);
	}
	else if (IsValid(Occupant) && Occupant->GuestState == EGuestState::Eating)
	{
		SeatMessage(TEXT("[餐桌] 客人正在用餐"), FColor::Cyan);
	}
	else if (!HasPlateOnSeat())
	{
		SeatMessage(TEXT("[餐桌] 暂时没有客人入座"), FColor::Silver);
	}
	return false;
}

bool AChairSeat::CanPlayerTakeOrder(AShootCharacter* Player) const
{
	return Player != nullptr
		&& IsValid(Occupant)        // 座位上有客人
		&& BoundOrder == nullptr    // 还没有点单
		&& Occupant->CanAcceptOrderByPlayer();
}

bool AChairSeat::TryTakeOrderByPlayer(AShootCharacter* Player)
{
	if (!CanPlayerTakeOrder(Player))
	{
		return false;
	}

	// 由客人向订单系统下单：AcceptOrderByPlayer → PlaceOrder 内部会调 BindOrder 把菜单绑到本座位
	if (!Occupant->AcceptOrderByPlayer())
	{
		SeatMessage(TEXT("[餐桌] 下单失败：订单系统未就绪或没有可用订单（详见 Output Log）"), FColor::Red, 5.f);
		return false;
	}

	// 兜底再绑定一次，保证座位上的菜单与刚生成的订单一致
	BindOrder(Occupant->CurrentOrder);

	SeatMessage(FString::Printf(TEXT("[餐桌] 已为客人下单【%s】菜品[%s]"),
		*BoundSetName, *JoinFoodIDs(BoundFoodIDs)), FColor::Green);
	return true;
}

bool AChairSeat::CanPlayerServeDish(AShootCharacter* Player) const
{
	return Player != nullptr
		&& BoundOrder != nullptr                                     // 座位已绑定菜单
		&& IsValid(Occupant)                                         // 客人还坐在上面
		&& Occupant->GuestState == EGuestState::WaitingFood          // 正在等餐
		&& !HasPlateOnSeat();                                        // 桌上还没有碟子
}

bool AChairSeat::TryServeDishByPlayer(AShootCharacter* Player, AItemActor* Dish)
{
	if (!Player || !Dish)
	{
		return false;
	}

	// 脏碟 / 空碟不是菜，不能上
	if (Dish->bIsDirtyPlate || Dish->bIsEmptyPlate)
	{
		SeatMessage(TEXT("[餐桌] 这不是做好的菜，无法上菜"), FColor::Yellow);
		return false;
	}

	// 座位必须先绑定菜单（玩家按 E 给客人下单后才会绑定）
	if (!BoundOrder)
	{
		if (IsValid(Occupant) && Occupant->CanAcceptOrderByPlayer())
		{
			SeatMessage(TEXT("[餐桌] 客人还没点单，请先空手按 E 给客人下单"), FColor::Yellow);
		}
		else
		{
			SeatMessage(TEXT("[餐桌] 这张餐桌还没有订单，不能上菜"), FColor::Red);
		}
		return false;
	}

	if (!IsValid(Occupant) || Occupant->GuestState != EGuestState::WaitingFood)
	{
		SeatMessage(TEXT("[餐桌] 客人当前不在等餐状态，无法上菜"), FColor::Red);
		return false;
	}

	if (HasPlateOnSeat())
	{
		SeatMessage(TEXT("[餐桌] 桌上已有碟子，请先按 E 收走空碟"), FColor::Yellow);
		return false;
	}

	// 关键校验：手上菜品的 ID 集合必须与座位绑定的菜单完全一致（顺序无关、数量一致）
	const TArray<int32> DishFoodIDs = GetDishFoodIDs(Dish);
	if (!AreFoodIDsMatching(DishFoodIDs, BoundFoodIDs))
	{
		SeatMessage(FString::Printf(TEXT("[餐桌] 上菜失败：客人点的是[%s]，手上拿的是[%s]"),
			*JoinFoodIDs(BoundFoodIDs), *JoinFoodIDs(DishFoodIDs)), FColor::Red, 4.f);
		return false;
	}

	// 完成订单：订单系统内部再按“绑定该客人 + 菜品匹配”校验一次（会驱动分数/声誉/金钱，并让客人开始进食）
	UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	AOrderSystem* OS = (GI && GI->curLevelBP) ? GI->curLevelBP->orderSystem : nullptr;
	if (!OS || !OS->IsStarted)
	{
		SeatMessage(TEXT("[餐桌] 订单系统未就绪，无法上菜"), FColor::Red);
		return false;
	}
	if (!OS->PushFoodsForGuest(DishFoodIDs, Occupant))
	{
		SeatMessage(TEXT("[餐桌] 上菜失败：订单已失效或菜品不匹配"), FColor::Red);
		return false;
	}

	// 上菜成功：成品摆到本座位的挂点上（位置由蓝图里的 PlateSocket 调整），
	// 并交给客人记录，进食结束后由客人把它变成空碟继续留在挂点上。
	PlacePlateOnSocket(Dish);
	Occupant->PlateOnTable = Dish;

	// 订单已结算：清掉座位上的菜单绑定，等待下一位客人
	const int32 ServedFoodID = DishFoodIDs.Num() > 0 ? DishFoodIDs[0] : -1;
	ClearOrder();

	// 玩家手上已交出菜品：清空手持状态
	Player->ClearHeldItemState();

	SeatMessage(TEXT("[餐桌] 上菜成功，客人开始用餐"), FColor::Green);
	OnDishServed(Dish, ServedFoodID);
	return true;
}

bool AChairSeat::CanPlayerCollectPlate(AShootCharacter* Player) const
{
	return Player != nullptr
		&& IsValid(PlateOnSeat)
		&& PlateOnSeat->bIsEmptyPlate
		&& Player->GetPickingItem() == nullptr; // 一只手只能拿一个
}

bool AChairSeat::TryCollectPlateByPlayer(AShootCharacter* Player)
{
	if (!CanPlayerCollectPlate(Player))
	{
		return false;
	}

	AItemActor* Plate = PlateOnSeat;
	// PickUpByPlayer 内部会调用 OwningSeat->ClearDirtyPlate()：真正释放座位并广播 OnSeatFreedDelegate，
	// 同时把碟子变成拿在手上的“脏碟”。
	if (!Plate->PickUpByPlayer(Player))
	{
		return false;
	}

	PlateOnSeat = nullptr;
	DirtyPlate = nullptr;
	bHasDirtyPlate = false;

	// 碟子已在手上，清掉玩家身上的空碟可交互引用，避免重复收走
	if (Player->GetOverlappingEmptyPlate() == Plate)
	{
		Player->SetOverlappingEmptyPlate(nullptr);
	}

	SeatMessage(TEXT("[餐桌] 已收走空碟，座位空出"), FColor::Green);
	OnPlateCollected(Plate);
	return true;
}

// ==================== 交互球注册 ====================

void AChairSeat::OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (AShootCharacter* Player = Cast<AShootCharacter>(OtherActor))
	{
		Player->SetOverlappingChairSeat(this);

		// TipsMode = PlayerNear 时按“范围内玩家数”决定提示控件显隐
		TipsNearbyPlayerCount++;
		RefreshTipsWidget();
	}
}

void AChairSeat::OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AShootCharacter* Player = Cast<AShootCharacter>(OtherActor))
	{
		// 仅当仍指向本餐桌时才清空，避免多张餐桌重叠时互相串扰
		if (Player->GetOverlappingChairSeat() == this)
		{
			Player->SetOverlappingChairSeat(nullptr);
		}

		// TipsMode = PlayerNear 时按“范围内玩家数”决定提示控件显隐
		TipsNearbyPlayerCount = FMath::Max(0, TipsNearbyPlayerCount - 1);
		RefreshTipsWidget();
	}
}

void AChairSeat::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 调试：座位 / 挂点 / 占用客人可视化，便于排查占座与碟子摆放问题
	if (!bDrawDebugSeatLink)
	{
		return;
	}

	// 入座挂点：绿色小球 + 朝前箭头（位置与朝向都取自 SitSocket）
	const FTransform SitTransform = GetSitTransform();
	const FVector SeatLoc = SitTransform.GetLocation();
	DrawDebugSphere(GetWorld(), SeatLoc, 12.f, 8, FColor::Green, false, -1.f, 0, 2.f);
	DrawDebugLine(GetWorld(), SeatLoc,
		SeatLoc + SitTransform.GetRotation().GetForwardVector() * 40.f, FColor::Green, false, -1.f, 0, 2.f);

	// 碟子挂点：青色小球（摆放碟子的位置）
	const FVector PlateLoc = GetPlateSocketTransform().GetLocation();
	DrawDebugSphere(GetWorld(), PlateLoc, 10.f, 8, FColor::Cyan, false, -1.f, 0, 2.f);

	if (IsValid(Occupant))
	{
		const FVector GuestLoc = Occupant->GetActorLocation();
		// 座位 -> 占用客人 的连线
		DrawDebugLine(GetWorld(), SeatLoc, GuestLoc,
			bIsOccupied ? FColor::Yellow : FColor::Red, false, -1.f, 0, 2.f);
		// 占用客人位置标记
		DrawDebugSphere(GetWorld(), GuestLoc, 15.f, 8, FColor::Yellow, false, -1.f, 0, 2.f);
	}
	else if (bIsOccupied)
	{
		// 被标记占用但客人已失效：用红色标记座位，提示“幽灵占用”
		DrawDebugSphere(GetWorld(), SeatLoc, 16.f, 8, FColor::Red, false, -1.f, 0, 3.f);
	}
}
