// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShootCharacter.h"
#include "Interaction/InteractionConfig.h"
#include "StorageWarehouse.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class AShootCharacter;

/** 仓库内容变化广播（存入 / 取出时触发，蓝图刷新 UI） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWarehouseChanged);

/**
 * 仓库：玩家靠近后按 E 把背包里的物品全部转入仓库（手上物品不受影响）。
 * 仓库按物品类型（ItemClass）堆叠存储，复用与背包相同的 FInventorySlot 结构。
 * 蓝图可绑定 OnWarehouseChanged 刷新仓库 UI；外观 / 交互半径可在蓝图或实例上调整。
 */
UCLASS()
class SHOOT_API AStorageWarehouse : public AActor
{
	GENERATED_BODY()

public:
	AStorageWarehouse();

	/** 交互球：玩家进入范围时注册到角色（AShootCharacter::SetOverlappingWarehouse） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Warehouse")
	TObjectPtr<USphereComponent> InteractionSphere;

	/** 仓库外观模型（蓝图 / 视口里替换） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Warehouse")
	TObjectPtr<UStaticMeshComponent> WarehouseMesh;

	/** 交互半径（玩家需进入此范围才能按 E 存物）。
	 *  OnConstruction（编辑器视口）与 BeginPlay（运行时）都会强制应用到组件上 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Warehouse", meta = (ClampMin = "10.0"))
	float InteractionRadius = InteractionDistance::Counter;

	/** 编辑器里改 InteractionRadius 时立即写进组件，关卡视口中即可看到真实判定球 */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 把 InteractionRadius（× Shoot.InteractScale 倍率）写进交互球。
	 *  OnConstruction / BeginPlay / 运行时改倍率都会调用，必须幂等 */
	UFUNCTION()
	void ApplyInteractionScale();

	/** 仓库容量上限（槽位数；<=0 表示不限制） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Warehouse")
	int32 StorageCapacity = 0;

	/** 仓库存储的物品（按类型堆叠，与背包使用同一 FInventorySlot 结构） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Warehouse")
	TArray<FInventorySlot> StoredItems;

	/** 营业期间锁定仓库：存入 / 取出都被拒绝（打烊后解锁），堵住「打烊囤货、营业取回」的绕过。
	 *  营业时钟取不到时按「营业中」处理（即锁定），与角色打烊拾取重定向的兜底一致。默认开启。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Warehouse")
	bool bLockWhileBusinessOpen = true;

	/** 当前是否处于营业锁定（bLockWhileBusinessOpen 且营业中）。蓝图提示文案 / UI 可用它区分营业与打烊。 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Warehouse")
	bool IsLockedWhileOpen() const;

	/** 玩家按 E：把背包里的全部物品转入仓库（背包清空）。返回转入的“种类”数（受容量上限截断，未转入的留在背包）。 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Warehouse")
	int32 DepositFromInventory(AShootCharacter* Player);

	/** 取出指定槽位物品回到玩家背包（反向操作，方便取回）。成功返回 true。 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Warehouse")
	bool RetrieveToInventory(AShootCharacter* Player, int32 SlotIndex);

	/** 仓库当前种类数（槽位数） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Warehouse")
	int32 GetStoredCount() const { return StoredItems.Num(); }

	/** 读取某槽位显示名（蓝图 UI 用，不暴露整个数组） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Warehouse")
	FString GetStoredDisplayName(int32 SlotIndex) const;

	/** 仓库中某物品类的总存量（跨槽位求和；无则 0）。用于食材箱“跟仓库数量对应”模式。 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Warehouse")
	int32 GetStoredQuantity(TSubclassOf<AItemActor> ItemClass) const;

	/** 从仓库消耗一件指定物品类（库存 -1，归零移除该槽）。成功返回 true（仓库为空则 false）。 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Warehouse")
	bool ConsumeOne(TSubclassOf<AItemActor> ItemClass);

	/** 仓库内容变化广播（存入 / 取出时触发，蓝图刷新 UI） */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Warehouse")
	FOnWarehouseChanged OnWarehouseChanged;

	// ==================== 提示控件（TipsWidget） ====================

	/** 提示控件组件：玩家靠近时显示“按 E 存入仓库”等提示。控件类在蓝图里给 TipsWidgetComp 的 Widget Class 配置。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Warehouse|Tips")
	TObjectPtr<UWidgetComponent> TipsWidgetComp;

	/** 提示控件相对仓库的位置偏移（蓝图视口里也可直接拖） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Warehouse|Tips")
	FVector TipsOffset = FVector(0.f, 0.f, 120.f);

	/** 玩家靠近 / 离开时通知蓝图（可借此更新提示文字，如“按 E 存入仓库（背包 X 件）”）。控件类由蓝图在 TipsWidgetComp 上配置。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Warehouse|Tips")
	void OnPlayerNearby(bool bNear);

	/** 显隐提示控件（显示前确保控件实例已创建） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Warehouse|Tips")
	void SetTipsVisible(bool bVisible);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};
