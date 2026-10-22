// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Engine/DataTable.h"
#include "MenuData.generated.h"

class AActor;

/**
 * 
 */
USTRUCT(BlueprintType)
 struct  FMenuData : public FTableRowBase
{
	GENERATED_BODY()

	public:
	UPROPERTY(EditAnywhere,BlueprintReadWrite, meta=(ToolTip="菜品所需食材列表：键为食材ItemID（详见DT_Foods数据表），值为该食材所需数量。"))
	TMap<int,int> MenuItems; // int-int 食材ItemID-数量 ItemID查看DT_Foods
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	FString FoodName;
	//-1:err,-2:not init
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	int32 FoodID;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	float CookingTime;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	bool CanOverCook;
	/** 烹饪完成后生成的成品类（如"番茄炒鸡蛋"菜品对应的成品） */
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	TSubclassOf<AActor> CookedActorClass;

	// ========== 成品品质奖励（上菜时按成品品质额外结算，叠加在基础菜价/声誉之上） ==========
	/** 佳品：额外金钱（在订单基础金钱之外） */
	UPROPERTY(EditAnywhere,BlueprintReadWrite, Category = "Quality")
	int32 PremiumBonusMoney = 0;
	/** 佳品：额外声誉 */
	UPROPERTY(EditAnywhere,BlueprintReadWrite, Category = "Quality")
	int32 PremiumBonusReputation = 0;
	/** 次品：额外金钱（可设为负数表示扣钱） */
	UPROPERTY(EditAnywhere,BlueprintReadWrite, Category = "Quality")
	int32 InferiorBonusMoney = 0;
	/** 次品：额外声誉（可设为负数表示扣声誉） */
	UPROPERTY(EditAnywhere,BlueprintReadWrite, Category = "Quality")
	int32 InferiorBonusReputation = 0;

	int GetNumOfItems() const
	{
		int sum = 0;
		for (const TPair<int, int>& foodMat : MenuItems) //遍历当前容器食材
		{
			sum += foodMat.Value;
		}
		return sum;
	}
};

// 注：原 FLevelInfo 已合并进 FLevelGoalRow（见 Game/LevelGoalData.h），本文件不再单独定义。
// FLevelGoalRow 现在同时承载“关卡目标条件”与“关卡入口/选择信息”（LevelPath / LevelDes / BackgroundTexture 等）。