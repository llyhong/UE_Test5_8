// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "HitInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI, Blueprintable, BlueprintType)
class UHitInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class SHOOT_API IHitInterface
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "My Interface")
	void GetHit(const FVector& ImpactPoint, AActor* Hitter);
};
