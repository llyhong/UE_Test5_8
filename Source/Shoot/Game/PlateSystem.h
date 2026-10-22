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


	UFUNCTION(BlueprintCallable)
	void AddPlate();

	UFUNCTION(BlueprintCallable)
	void RemovePlate();

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FPlateEvent OnAddPlate;
};
