// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPC/ChairTypes.h"
#include "ChairSeat.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class UWidgetComponent;
class UChairTipsWidget;
class AGuestCharacter;
class AItemActor;
class AShootCharacter;
class UOrderHandle;

/** 座位被“空碟”回收而释放时广播（由客人管理器监听以安排下一位客人入座） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSeatFreedSignature);

/** 座位菜单变化时广播：传出最新的菜品 ID 列表（清空时为长度为 0 的数组），供 UI 刷新菜单 ICON */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChairMenuChangedSignature, const TArray<int32>&, FoodIDs);

/**
 * 餐桌 / 上菜台：客人可坐下的座位，同时承担“点单 + 上菜 + 收空碟”的交互。
 *
 * 交互闭环（玩家靠近后按 E，见 Pick() 内的 E 分发）：
 * 1) 客人坐下等待点单 → 按 E 给客人下单，并把该客人的订单（菜单）绑定到本座位（BoundOrder / BoundFoodIDs）；
 * 2) 玩家端着做好的菜按 E 上菜 → 校验手上菜品与座位绑定的菜单是否完全一致，一致才完成订单；
 * 3) 客人吃完 → 空碟留在本座位的碟子挂点（PlateSocket）上，座位继续保持占用；
 * 4) 玩家把空碟收走 → 座位才真正释放，下一位客人才能入座。
 *
 * 两个挂点都是普通 SceneComponent，位置 / 朝向在蓝图或关卡实例里直接拖动即可调整：
 * - SitSocket：客人坐下时的位置与朝向（客人寻路终点、坐下吸附都以它为准）；
 * - PlateSocket：碟子（成品 / 空碟）的摆放位置。
 */
UCLASS()
class SHOOT_API AChairSeat : public AActor
{
	GENERATED_BODY()

public:
	AChairSeat();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// ==================== 组件 ====================

	/** 凳子网格体 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair|Components")
	TObjectPtr<UStaticMeshComponent> SeatMesh;

	/** 碟子挂点：上菜后的成品、客人留下的空碟都吸附到这里。
	 *  在蓝图视口（或关卡实例）里选中 PlateSocket 直接拖动，即可自由调整碟子的摆放位置与朝向。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair|Components")
	TObjectPtr<USceneComponent> PlateSocket;

	/** 入座挂点：客人坐下时的位置与朝向（默认在凳子上方 50cm）。
	 *  在蓝图视口（或关卡实例）里选中 SitSocket 直接拖动，即可自由调整客人落在哪里、朝哪边坐；
	 *  客人寻路的终点与到达判定也取该挂点的位置。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair|Components")
	TObjectPtr<USceneComponent> SitSocket;

	/** 交互球：玩家进入范围后按 E 点单 / 上菜 / 收空碟 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair|Components")
	TObjectPtr<USphereComponent> InteractionSphere;

	/** 交互球半径（玩家需在此范围内才能按 E 与餐桌交互） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chair", meta = (ClampMin = "10.0"))
	float InteractionRadius = 180.f;

	// ==================== 座位 ====================

	/** 该座位是否被占用（客人坐着，或被未回收的碟子占着） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair")
	bool bIsOccupied = false;

	/** 当前占用该座位的客人 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair")
	TObjectPtr<AGuestCharacter> Occupant;

	/** 座位是否可以入座：既没有客人，也没有未回收的碟子 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair")
	bool CanOccupy() const;

	/** 占用座位（成功返回 true；已被占用 / 桌上还有碟子返回 false） */
	UFUNCTION(BlueprintCallable, Category = "Chair")
	bool Occupy(AGuestCharacter* Guest);

	/** 释放座位（客人超时离场等场景调用）；若桌上仍有碟子则保持占用，由 ClearDirtyPlate 释放 */
	UFUNCTION(BlueprintCallable, Category = "Chair")
	void Release();

	/** 强制释放座位：连同桌上残留的碟子一起清掉（关卡收尾 / 清场时用，不广播 OnSeatFreedDelegate） */
	UFUNCTION(BlueprintCallable, Category = "Chair")
	void ForceRelease();

	/** 返回坐下时的世界变换（位置与朝向完全取自 SitSocket 挂点） */
	UFUNCTION(BlueprintCallable, Category = "Chair")
	FTransform GetSitTransform() const;

	// ==================== 订单（菜单绑定） ====================

	/** 本座位当前绑定的订单：客人点单成功时写入，上菜校验 / UI 显示都用它 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair|Order")
	TObjectPtr<UOrderHandle> BoundOrder = nullptr;

	/** 绑定订单的菜品 ID 列表（副本，蓝图不解析订单也能直接显示菜单） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair|Order")
	TArray<int32> BoundFoodIDs;

	/** 绑定订单的套餐名（仅用于提示 / UI） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair|Order")
	FString BoundSetName;

	/** 把订单（菜单）绑定到本座位；传 nullptr 等同 ClearOrder */
	UFUNCTION(BlueprintCallable, Category = "Chair|Order")
	void BindOrder(UOrderHandle* Order);

	/** 清空本座位绑定的订单 */
	UFUNCTION(BlueprintCallable, Category = "Chair|Order")
	void ClearOrder();

	/** 本座位是否已绑定订单（客人已点单且尚未上菜） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Order")
	bool HasOrder() const { return BoundOrder != nullptr; }

	/** 当前菜单的菜品 ID 列表（不合并重复项，顺序与订单一致）；未点单时为空数组。
	 *  UI 拿它去查菜品图标（如 DT_FoodID 的 OriFoodIcon / MachineIcon）即可铺出菜单 ICON */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Order")
	TArray<int32> GetMenuFoodIDs() const;

	/** 当前菜单的套餐名（取自订单的 SetName）；未点单时为空 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Order")
	FString GetMenuSetName() const;

	/** 当前绑定到本座位的订单句柄（未点单时为 null，可用于取 IamgeID / 剩余时间等） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Order")
	UOrderHandle* GetMenuOrder() const { return BoundOrder; }

	/** 菜单变化广播：下单成功 / 上菜后清空（空数组）时触发，UI 绑定后无需轮询 */
	UPROPERTY(BlueprintAssignable, Category = "Chair|Order")
	FOnChairMenuChangedSignature OnMenuChanged;

	/** 比较两组菜品 ID 是否完全一致（顺序无关、数量必须一致，与订单系统的匹配规则相同） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Order")
	static bool AreFoodIDsMatching(const TArray<int32>& A, const TArray<int32>& B);

	/** 取碟子上的菜品 ID 列表：优先 FoodIdArray，为空则退回单个 MenuFoodID */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Order")
	static TArray<int32> GetDishFoodIDs(AItemActor* Dish);

	// ==================== 碟子 / 空碟 ====================

	/** 当前摆在挂点上的碟子（上菜后的成品，或客人留下的空碟） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair|Plate")
	TObjectPtr<AItemActor> PlateOnSeat = nullptr;

	/** 当前占据本座位的空碟（无则为 nullptr）。仅空碟模式下有效 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair|Plate")
	TObjectPtr<AItemActor> DirtyPlate = nullptr;

	/** 是否被空碟占用（客人已离座，但空碟尚未被回收） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair|Plate")
	bool bHasDirtyPlate = false;

	/** 挂点上是否有碟子（成品或空碟）。为真时不允许新客人入座 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Plate")
	bool HasPlateOnSeat() const;

	/** 碟子挂点的世界变换（蓝图摆放物体 / 调试用） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Plate")
	FTransform GetPlateSocketTransform() const;

	/** 把碟子吸附到挂点上（位置完全由蓝图里的 PlateSocket 决定），并停用拾取/腐烂/碰撞 */
	UFUNCTION(BlueprintCallable, Category = "Chair|Plate")
	bool PlacePlateOnSocket(AItemActor* Plate);

	/** 客人吃完离座：空碟留在挂点上并继续占据本座位，直到玩家按 E 收走 */
	UFUNCTION(BlueprintCallable, Category = "Chair|Plate")
	void SetDirtyPlate(AItemActor* Plate);

	/** 回收空碟时调用：真正释放座位并广播 OnSeatFreedSignature */
	UFUNCTION(BlueprintCallable, Category = "Chair|Plate")
	void ClearDirtyPlate();

	// ==================== 菜单提示控件（TipsWidget） ====================

	/** 菜单提示控件：默认挂在餐桌上方，用来显示当前客人点的菜单 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair|Tips")
	TObjectPtr<UWidgetComponent> TipsWidgetComp;

	/** 提示控件的显示时机：默认只在绑定了菜单（等待上菜）时显示 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chair|Tips")
	EChairSeatTipsMode TipsMode = EChairSeatTipsMode::OrderOnly;

	/** 当前桌况（空 / 入座中 / 等待点单 / 等待上菜 / 用餐中 / 待收空碟），提示控件按它决定显示什么 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	EChairTipsState GetSeatState() const;

	/** 取提示控件实例（未配置控件类 / 控件尚未创建时返回 nullptr） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	UChairTipsWidget* GetTipsWidget() const;

	/** 刷新提示控件：先把 OwnerSeat 交给控件、再按当前桌况决定显隐、最后通知蓝图重画菜单。
	 *  座位 / 订单 / 碟子状态变化时本类会自动调用，蓝图也可手动调用 */
	UFUNCTION(BlueprintCallable, Category = "Chair|Tips")
	void RefreshTipsWidget();

	// ==================== 玩家交互（E 键） ====================

	/**
	 * 玩家按 E 时由 AShootCharacter::Pick 调用，按当前状态自动决定做点单 / 上菜 / 收空碟。
	 * @return true 表示本次交互已被本餐桌消费（调用方可直接 return）；false 表示未拦截，可继续后续逻辑
	 */
	UFUNCTION(BlueprintCallable, Category = "Chair|Interact")
	bool TryInteractByPlayer(AShootCharacter* Player);

	/** 是否允许玩家给本座位的客人下单：客人已坐下等待点单且本座位还没有订单 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Interact")
	bool CanPlayerTakeOrder(AShootCharacter* Player) const;

	/** 玩家按 E 给客人下单：生成订单并绑定到本座位（菜单绑定）。返回是否成功 */
	UFUNCTION(BlueprintCallable, Category = "Chair|Interact")
	bool TryTakeOrderByPlayer(AShootCharacter* Player);

	/** 是否允许玩家上菜：座位已绑定订单 + 客人正在等餐 + 桌上还没有碟子 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Interact")
	bool CanPlayerServeDish(AShootCharacter* Player) const;

	/**
	 * 玩家按 E 上菜：校验手上菜品与座位绑定的菜单是否完全一致，一致才完成订单并把菜摆到挂点上。
	 * @return 是否上菜成功（失败时菜品仍由玩家持有）
	 */
	UFUNCTION(BlueprintCallable, Category = "Chair|Interact")
	bool TryServeDishByPlayer(AShootCharacter* Player, AItemActor* Dish);

	/** 是否允许玩家收走桌上的空碟：手上有空碟且玩家空手 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Interact")
	bool CanPlayerCollectPlate(AShootCharacter* Player) const;

	/** 玩家按 E 收走空碟：碟子变脏碟拿在手上，座位立即释放（下一位客人才能入座） */
	UFUNCTION(BlueprintCallable, Category = "Chair|Interact")
	bool TryCollectPlateByPlayer(AShootCharacter* Player);

	// ==================== 事件 / 调试 ====================

	/** 座位被“空碟回收”释放时广播（客人管理器监听以安排下一位客人入座） */
	UPROPERTY(BlueprintAssignable, Category = "Chair")
	FOnSeatFreedSignature OnSeatFreedDelegate;

	/** 玩家下单成功、菜单绑定到本座位时通知蓝图（做点单 UI / 特效 / 音效） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Chair|Events")
	void OnOrderBound(int32 OrderId, const TArray<int32>& FoodIDs);

	/** 上菜成功时通知蓝图（做上菜特效 / 飘字） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Chair|Events")
	void OnDishServed(AItemActor* Dish, int32 FoodID);

	/** 空碟被收走、座位释放时通知蓝图 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Chair|Events")
	void OnPlateCollected(AItemActor* Plate);

	/** 是否绘制调试信息：座位/挂点/到当前占用客人的连线，方便排查占座与碟子摆放 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chair|Debug")
	bool bDrawDebugSeatLink = false;

protected:
	/** 按 TipsMode 与当前桌况更新控件显隐（显示时顺带确保控件实例已创建） */
	void UpdateTipsVisibility();

	/** 交互球：玩家进入范围时注册到玩家（AShootCharacter::SetOverlappingChairSeat） */
	UFUNCTION()
	void OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** 交互球：玩家离开范围时反注册 */
	UFUNCTION()
	void OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** 交互范围内的玩家数量（TipsMode = PlayerNear 时用来决定显隐） */
	UPROPERTY(VisibleInstanceOnly, Category = "Chair|Tips")
	int32 TipsNearbyPlayerCount = 0;
};
