// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Engine/DataTable.h"
#include "DataClass.generated.h"
// 定义结构体
USTRUCT(BlueprintType) // BlueprintType 使其可用于蓝图
struct FPlayerInfo
{
	GENERATED_BODY() // 必须包含的宏

	// 定义属性
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayerInfo")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayerInfo")
	int32 ModelID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayerInfo")
	int32 Coin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayerInfo")
	int32 UserID;

};

/**
 * 订单（一单菜单）——只描述"这一单点了哪些菜"，不存任何可派生数值。
 *
 * FoodIDs 的每个元素 = 菜品表 MenuDataTable（FMenuData / 菜品行 RowName）的 FoodID，
 * 不再是"食材 ItemID"；食材由菜品行 FMenuData::MenuItems 决定。
 *
 * 以下信息一律实时按菜品表派生，不再另存字段：
 *   UOrderHandle::GetOrderPrice()        = 各菜品 FMenuData::Price 之和
 *   UOrderHandle::GetOrderMaxWaitTime()  = 各菜品 FMenuData::MaxWaitTime 的最大值
 *   UOrderHandle::GetOrderDisplayName()  = 各菜品 FMenuData::FoodName 拼接（多菜用 "+" 连接）
 *   UOrderHandle::GetMachineImageIDs()   = 各菜品 FMenuData::MachineID 去重列表
 */
USTRUCT(BlueprintType)
struct  FOrderData 
{
	GENERATED_BODY()
public:
	/** 本单包含的菜品 ID：每一项都是菜品表（MenuDataTable / FMenuData）的 FoodID。
	 *  一般 1 道；自定义套餐（ULevelOrderCfg::Orders）可多道。 */
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	TArray<int32> FoodIDs;
};

USTRUCT(BlueprintType)
struct  FAppearOrder 
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	int AppearOrderID;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	float AppearTime;
};

USTRUCT(BlueprintType) // BlueprintType 使其可用于蓝图
struct FFoodInfo  : public FTableRowBase
{
	GENERATED_BODY() // 必须包含的宏
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	int32 MachineImageID;
	// 定义属性
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UTexture2D> MachineIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TObjectPtr<UTexture2D>> OriFoodIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Title;
};

UCLASS(BlueprintType)
class ULevelOrderCfg : public UDataAsset 
{
	GENERATED_BODY()
public:
	/** 可选的"自定义套餐"：需要一单多道菜时才配（只填 FoodIDs，每项 = 菜品表的 FoodID）。
	 *  留空则订单池完全由菜品表（MenuDataTable）里 Probabilitys>0 的菜品各成一单。
	 *  价格 / 等待时间 / 套餐名 / 机器图片 / 权重一律由菜品表派生，不再在这里配置。 */
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	TArray<FOrderData> Orders;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	TArray<FAppearOrder> AppearTimes;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	int MaxWaitOrderNum;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	float TargetScore;
};

// UCLASS(BlueprintType)
// class UOriFoodInfo : public UDataAsset
// {
// 	GENERATED_BODY()
// public:
// 	UPROPERTY(EditAnywhere,BlueprintReadWrite)
// 	int MachineImageID;
// 	UPROPERTY(EditAnywhere,BlueprintReadWrite)
// 	TArray<FFoodInfo> Orders;
// };