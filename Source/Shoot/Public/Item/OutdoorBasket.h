// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Item/ItemActor.h"
#include "ShootCharacter.h"
#include "OutdoorBasket.generated.h"

class AStorageWarehouse;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBasketChanged);

/**
 * 外出背包（菜篮子）：玩家按 E 拿在手上，外出采摘的食材默认装进这里；
 * 带着它和仓库交互才把食材转入仓库，随后背包脱手放在仓库旁边。
 * 容量按“总件数”计算，可通过 UpgradeCapacity 升级扩大。
 */
UCLASS()
class SHOOT_API AOutdoorBasket : public AItemActor
{
	GENERATED_BODY()

public:
	AOutdoorBasket();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void AttachMeshToSocket(USceneComponent* InParent, const FName& InSocketName) override;
	virtual void DeAttachMeshToSocket() override;

	/** 挂在角色手上时改用这个骨骼插槽（父级没有请求的插槽时才用它兜底） */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Basket")
	FName HeldSocketName = FName("hand_rSocket");

	/** 挂手上的微调：篮子枢轴在提手顶部，稍作偏移可避免穿手；仅在挂到角色骨架时生效 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Basket")
	FVector HeldMeshOffset = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Basket")
	FRotator HeldMeshRotation = FRotator::ZeroRotator;

	/** 拿在手上时相对关卡摆放尺寸的缩放。角色骨架自带 2.25 倍缩放，不补偿会把篮子撑大 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Basket", meta = (ClampMin = "0.05"))
	float HeldMeshScale = 1.f;

	/** 拾取判定球半径在包围盒半径之外再留的余量 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Basket", meta = (ClampMin = "0.0"))
	float InteractionRadiusPadding = 6.f;

	/** 容量（总件数）。收纳已满时采摘不生效，避免玩家白跑一趟 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Basket", meta = (ClampMin = "1"))
	int32 BasketCapacity = 10;

	/** 当前收纳内容（与仓库/背包同一 FInventorySlot 结构，同类堆叠） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Basket")
	TArray<FInventorySlot> BasketItems;

	/** 已装件数（跨槽位求和） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Basket")
	int32 GetUsedCount() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Basket")
	int32 GetRemainingSpace() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Basket")
	bool IsBasketFull() const { return GetRemainingSpace() <= 0; }

	/** 装入一件（或一批）物品：容量不足时整批失败；成功后销毁世界上的物品 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Basket")
	bool TryAddItem(AItemActor* Item, int32 Quantity = 1);

	/** 设置容量（升级 / 调试用） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Basket")
	void SetBasketCapacity(int32 NewCapacity);

	/** 扩大容量（升级）：返回新容量 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Basket")
	int32 UpgradeCapacity(int32 Extra);

	/** 清空收纳内容（不负责生成世界物品） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Basket")
	void EmptyBasket();

	/** 仓库存入流程用：把背包放在仓库旁边（不参与物理，避免穿地/滚走） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Basket")
	void PlaceBesideWarehouse(AActor* Warehouse, const FVector& PlayerFeetLocation);

	/** 收纳内容变化广播（UI 显示 3/10 等） */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Basket")
	FOnBasketChanged OnBasketChanged;

	/** AStorageWarehouse 从背包里搬走已入库的槽位时使用 */
	TArray<FInventorySlot>& GetItemsMutable() { return BasketItems; }

private:
	/** 篮子网格枢轴在提手顶部：把拾取球心对齐到网格包围盒中心，否则球浮在模型上方 */
	void FitInteractionToMesh();

	/** 网格最低点在 Actor 本地空间里的 Z（枢轴抬高，落地摆放时要补这个偏移） */
	float GetMeshBottomLocalZ() const;

	/** 挂到手上前记录的关卡摆放缩放，脱手时还原 */
	FVector CachedWorldScaleBeforeAttach = FVector(1.f);
	bool bScaleCompensated = false;
};
