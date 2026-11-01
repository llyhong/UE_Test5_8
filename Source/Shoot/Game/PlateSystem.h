// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlateSystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPlateEvent); 
UCLASS()
class SHOOT_API APlateSystem : public AActor
{
	GENERATED_BODY()
	
public:	
	APlateSystem();

protected:
	virtual void BeginPlay() override;

public:	
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable)
	void InitParam(int PlateCount,float SpawnPlateTime);

	/** 设置本关碟子总数（洗碗机可配置）；并把当前可用碟子数钳制在总数内。
	 *  @param bFillAvailable true = 同时把"当前可用碟子数"补满到总数（开局初始化用：
	 *        开局时所有碟子都是干净的，所以可用数 = 总数）。false = 仅改上限，不补碟子。 */
	UFUNCTION(BlueprintCallable)
	void SetTotalCount(int32 NewTotal, bool bFillAvailable = false);

	/** 把可用碟子数直接补满到总数（开局重置用） */
	UFUNCTION(BlueprintCallable)
	void RefillToFull();

	/** 当前可用碟子数：无限模式返回极大值（UI 可显示为 ∞）；有限模式返回 CurOrderIndex */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Gameplay")
	int32 GetAvailablePlates() const { return bInfinitePlates ? TNumericLimits<int32>::Max() : CurOrderIndex; }

	/** 是否无限碟子（由空碟玩法总开关 bUseDirtyPlateFlow 取反驱动：不收空碟=false → 无限） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Gameplay")
	bool IsInfinitePlates() const { return bInfinitePlates; }

	/** 切换无限碟子模式：true=无限（RemovePlate/AddPlate 皆为 no-op，无需回收）；
	 *  false=恢复有限，并把可用碟子补满到总数（恢复收碟闭环） */
	UFUNCTION(BlueprintCallable, Category = "Gameplay")
	void SetInfinite(bool bInfinite);

	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	bool bInfinitePlates = false;

	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	int CurOrderIndex = 3;
	
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	int TotalCount = 3;

	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	float TimeAccumulator = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	float TotalTime = 10.0f;
	
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	bool IsStarted = false;

	/** 是否自动补充碟子（每隔 TotalTime 自动 +1 直到 TotalCount）。
	 *  洗碗机模式下应关闭（默认 false）：碟子只能通过玩家把脏碟交还给洗碗机来回收。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay")
	bool bAutoRegenPlates = false;


	UFUNCTION(BlueprintCallable)
	void AddPlate();

	UFUNCTION(BlueprintCallable)
	void RemovePlate();

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FPlateEvent OnAddPlate;
};
