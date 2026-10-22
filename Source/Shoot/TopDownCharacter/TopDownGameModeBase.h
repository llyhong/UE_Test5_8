// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TopDownGameModeBase.generated.h"

/**
 * 
 */
UCLASS(abstract)
class SHOOT_API ATopDownGameModeBase : public AGameModeBase
{
	GENERATED_BODY()
public:
    	/** Constructor */
    	ATopDownGameModeBase();
};
