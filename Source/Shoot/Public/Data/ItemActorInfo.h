// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ItemActorInfo.generated.h"

/**
 * 物品信息数据表行结构
 */
USTRUCT(BlueprintType)
struct FItemActorInfo : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 物品 ID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ItemID;

	/** 物品描述 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ItemDes;
};
