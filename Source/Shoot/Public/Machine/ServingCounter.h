// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "Data/MenuData.h"
#include "ServingCounter.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class AItemActor;
class AShootCharacter;
class AGuestCharacter;

/**
 * 上菜台：玩家把手上做好的成品（菜品）提交到这里。
 * - 校验该成品的 MenuFoodID 是否存在于指定菜单数据表（即“菜单里面有”）。
 * - 若在菜单中，则找一个正在等餐的客人（EGuestState::WaitingFood）并让其开始进食（成功上菜）。
 * - 若不在菜单中，则上菜失败（成品不被消耗，仍由玩家持有）。
 *
 * 在关卡中摆放此 Actor 即可；靠近时由重叠球把自身注册到玩家（AShootCharacter::OverlappingCounter）。
 */
UCLASS(Blueprintable)
class SHOOT_API AServingCounter : public AActor
{
	GENERATED_BODY()

public:
	AServingCounter();

	virtual void BeginPlay() override;

	/** 上菜台外观模型（可在蓝图/实例上选择网格） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* CounterMesh;

	/** 交互范围：玩家进入后注册为可提交菜品的上菜台 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USphereComponent* InteractionSphere;

	/** 菜单数据表（RowStruct = FMenuData）。用于校验提交的成品是否在菜单中 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Serve", meta = (RowStruct = "MenuData"))
	UDataTable* MenuDataTable = nullptr;

	/** 是否要求必须存在“正在等餐”的客人才算上菜成功（否则仅校验菜单通过后即成功） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Serve")
	bool bRequireWaitingGuest = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Serve")
	bool bUseMenuControll = true;

	/** 上菜是否把成品送到客人座位处（false 则维持旧行为：菜在柜台即销毁） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Serve")
	bool bDeliverToSeat = true;

	/** 成品送到客人位置后停留时长（秒）：停留结束才销毁，让玩家看到“菜到了客人面前” */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Serve", meta = (ClampMin = "0.1"))
	float ServeArriveDelay = 1.0f;

	/**
	 * 提交菜品：玩家把手上的成品交到上菜台。
	 * @param Dish 玩家当前手持的成品（AItemActor，需带有效的 MenuFoodID）
	 * @return true 表示成功上菜（Dish 已被消费并销毁）；false 表示失败（Dish 仍由玩家持有）
	 */
	UFUNCTION(BlueprintCallable, Category = "Serve")
	bool SubmitDish(AItemActor* Dish);

	/** 上菜成功通知（蓝图可重载：播放特效 / 加分 / UI 提示） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Serve")
	void OnServeSuccess(int32 FoodID);

	/** 上菜失败通知（蓝图可重载：播放提示）。Reason: "NotInMenu" / "NoWaitingGuest" */
	UFUNCTION(BlueprintImplementableEvent, Category = "Serve")
	void OnServeFail(int32 FoodID, const FString& Reason);

	UFUNCTION()
	void OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

protected:
	/** 校验菜品 ID 是否存在于菜单数据表中 */
	bool IsFoodInMenu(int32 FoodID) const;

	/** 按菜品 ID 从菜单数据表取出对应 FMenuData 行（找不到返回 false） */
	bool GetMenuDataForFood(int32 FoodID, FMenuData& OutMenuData) const;

	/** 上菜成功时，按成品品质查菜单配置结算额外金钱 / 声誉奖励 */
	void ApplyQualityReward(AItemActor* Dish);

	/** 找到一个正在等餐（WaitingFood）的客人，找不到返回 nullptr */
	AGuestCharacter* FindWaitingGuest() const;

	/**
	 * 把成品送到客人座位处：移动到客人位置（略抬高），停用碰撞，停留 ServeArriveDelay 后销毁。
	 * 视觉上“上菜到客人位置”；传入空客人则直接销毁（退回旧行为）。
	 */
	void DeliverDishToSeat(AItemActor* Dish, AGuestCharacter* Guest);
};
