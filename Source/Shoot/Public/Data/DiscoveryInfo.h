// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "DiscoveryInfo.generated.h"

/**
 * 首次发现配置：DataTable（RowStruct = DiscoveryInfo）可选配置。
 * 行名与物品信息表的行名一致（如 "7"）；未配置的物品种类会回退到 FItemActorInfo 的图标/描述 + GameInstance 默认奖励。
 */
USTRUCT(BlueprintType)
struct FDiscoveryInfo : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 弹窗标题（物品名） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Discovery")
	FString DisplayName;

	/** 弹窗描述 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Discovery")
	FString Description;

	/** 弹窗图标（留空则用物品信息表的 Icon） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Discovery")
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** 首次发现奖励金币 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Discovery")
	int32 RewardMoney = 0;

	/** 首次发现奖励声誉 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Discovery")
	int32 RewardReputation = 0;
};

/** 首次发现广播：Info = 展示信息，ItemKey = 物品唯一键（DT 行名） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnItemDiscovered, const FDiscoveryInfo&, Info, FName, ItemKey);
