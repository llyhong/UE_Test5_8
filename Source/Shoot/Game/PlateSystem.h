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

	/** 设置本关碟子总数（洗碗机可配置）；并把当前可用碟子数钳制在总数内 */
	UFUNCTION(BlueprintCallable)
	void SetTotalCount(int32 NewTotal);

	/** 当前可用碟子数（= CurOrderIndex） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Gameplay")
	int32 GetAvailablePlates() const { return CurOrderIndex; }
	
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
