// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Dishwasher.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class ULocalPlayer;
class AItemActor;
class APlateSystem;
class AShootCharacter;

/**
 * 洗碗机：玩家把从桌上收走的“脏碟”交还到这里清洗。
 * - 按 E 交还：碟子消失（Destroy），可用碟子数 +1（APlateSystem::AddPlate，上限为碟子总数）。
 * - 碟子总数(TotalCount)可在此配置：>0 时 BeginPlay 覆盖盘子系统(APlateSystem)的总数。
 * - 消耗侧：烹饪机完成并生成成品时消耗一个碟子（APlateSystem::RemovePlate，见 ACookingMachine::SpawnCookedActor）。
 *
 * 在关卡中摆放此 Actor 即可；靠近时由重叠球把自身注册到玩家（AShootCharacter::OverlappingDishwasher）。
 */
UCLASS(Blueprintable)
class SHOOT_API ADishwasher : public AActor
{
	GENERATED_BODY()

public:
	ADishwasher();

	virtual void BeginPlay() override;

	/** 洗碗机外观模型（可在蓝图/实例上选择网格） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* DishwasherMesh;

	/** 交互范围：玩家进入后注册为可交还碟子的洗碗机 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USphereComponent* InteractionSphere;

	/** 交互提示控件（如“按 E 洗碟子”）：靠近时显示，离开隐藏；蓝图可在组件上配置 Widget Class */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWidgetComponent> TipsWidget;

	/** 本关碟子总数：>0 时在 BeginPlay 覆盖盘子系统(APlateSystem)的 TotalCount；
	 *  <=0 表示沿用盘子系统已有的配置（InitPlateSystem 传入的值）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dishwasher", meta = (ClampMin = "0"))
	int32 TotalPlates = 0;

	/** 是否只接受“脏碟”（玩家从桌上收走的碟子）。关闭则手持任何物品都能交还。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dishwasher")
	bool bRequireDirtyPlate = true;

	/**
	 * 交还一个碟子：可用碟子数 +1，碟子消失。
	 * @param Plate 玩家当前手持的碟子（脏碟）
	 * @return true 表示交还成功（碟子已被销毁）；false 表示失败（碟子仍由玩家持有）
	 */
	UFUNCTION(BlueprintCallable, Category = "Dishwasher")
	bool SubmitPlate(AItemActor* Plate);

	/** 当前可用碟子数（读取盘子系统） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dishwasher")
	int32 GetAvailablePlates() const;

	/** 碟子总数（读取盘子系统） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dishwasher")
	int32 GetTotalPlates() const;

	/** 交还成功通知（蓝图可重载：播放特效 / 音效 / UI 提示） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Dishwasher")
	void OnPlateReturned(int32 AvailablePlates);

	/** 交还失败通知（蓝图可重载：播放提示）。Reason: "NoPlate" / "NotDirty" / "NoPlateSystem" */
	UFUNCTION(BlueprintImplementableEvent, Category = "Dishwasher")
	void OnPlateReturnFailed(const FString& Reason);

	UFUNCTION()
	void OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

protected:
	/** 取盘子系统：优先关卡蓝图上的实例，否则在场景中查找第一个 */
	APlateSystem* FindPlateSystem() const;

	/** 当前占用提示控件的本地玩家（多人分屏隔离用，避免提示被其它玩家误显示） */
	UPROPERTY()
	TObjectPtr<ULocalPlayer> LocalPlayer;
};
