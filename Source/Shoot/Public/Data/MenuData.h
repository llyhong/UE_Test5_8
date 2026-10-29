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

	/** 菜品图标（成品图标）：UI 显示菜单 / 订单时使用（按 FoodID 取，见 UFoodSystem::GetFoodIcon）。
	 *  各"食材"的图标不在这里配，请到物品表 DT_ItemInfo（FItemActorInfo::Icon）按食材行配置。 */
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	TObjectPtr<UTexture2D> MenuIcon = nullptr;

	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	FString FoodName;
	//-1:err,-2:not init
	// 行名（RowName）约定就是 FoodID（如 "3"）：行名为纯数字时以行名为准，本字段只做兼容兜底
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	int32 FoodID;
	/** 可制作该菜品的机器 ID（对应 ACookingMachine::MachineID）；
	 *  0 = 通用，任何灶台都能做（因此旧数据不填也不会被卡住） */
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	int32 MachineID = 0;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	float CookingTime;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	bool CanOverCook;
	/** 烹饪完成后生成的成品类（如"番茄炒鸡蛋"菜品对应的成品） */
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	TSubclassOf<AActor> CookedActorClass;

	// ========== 订单属性（原 ULevelOrderCfg::Orders / FOrderData 里的字段，已合并到菜品行） ==========
	/** 售价：完成含本菜品的订单时获得的金钱 / 分数（原 FOrderData::Price）。
	 *  多道菜组成的订单价格 = 各菜品 Price 之和（由 UOrderHandle::GetOrderPrice 实时汇总） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order")
	int32 Price = 0;

	/** 最长等待时间（秒）：超时算订单失败（原 FOrderData::MaxWaitTime）。
	 *  多道菜组成的订单取其中的最大值（由 UOrderHandle::GetOrderMaxWaitTime 实时汇总） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order")
	float MaxWaitTime = 60.f;

	/** 出现在本关订单池里的权重：越大越容易被点到；<=0 表示这道菜不会出现在订单里
	 *  （原 FOrderData::Probabilitys）。全为 0 时订单系统退化为均匀随机 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order")
	float Probabilitys = 1.f;

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

	/** MenuItems 里第一个食材的 ItemID（没有食材时返回 INDEX_NONE）。用于只显示一个"主图标"的场景 */
	int32 GetFirstItemID() const
	{
		for (const TPair<int, int>& Item : MenuItems)
		{
			return Item.Key;
		}
		return INDEX_NONE;
	}

	/** MenuItems 的食材 ItemID 列表（与 FoodIcon 的键一一对应） */
	TArray<int32> GetItemIDs() const
	{
		TArray<int32> Out;
		Out.Reserve(MenuItems.Num());
		for (const TPair<int, int>& Item : MenuItems)
		{
			Out.Add(Item.Key);
		}
		return Out;
	}

	/** 从行名解析菜品 ID：行名为纯数字（约定 RowName == FoodID）时返回该值，否则返回 INDEX_NONE */
	static int32 GetFoodIDFromRowName(FName RowName)
	{
		const FString RowNameStr = RowName.ToString();
		return RowNameStr.IsNumeric() ? FCString::Atoi(*RowNameStr) : INDEX_NONE;
	}

	/** 本行的菜品 ID：行名为数字时以行名为准，否则退回 FoodID 字段（兼容未改行名的旧数据） */
	int32 ResolveFoodID(FName RowName) const
	{
		const int32 RowNameID = GetFoodIDFromRowName(RowName);
		return RowNameID != INDEX_NONE ? RowNameID : FoodID;
	}
};

// 注：原 FLevelInfo 已合并进 FLevelGoalRow（见 Game/LevelGoalData.h），本文件不再单独定义。
// FLevelGoalRow 现在同时承载“关卡目标条件”与“关卡入口/选择信息”（LevelPath / LevelDes / BackgroundTexture 等）。