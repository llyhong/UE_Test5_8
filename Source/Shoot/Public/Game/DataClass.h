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

USTRUCT(BlueprintType)
struct  FOrderData 
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	TArray<int32> FoodIDs;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	FString SetName;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	int Price;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	float MaxWaitTime;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	float Probabilitys;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	int IamgeID;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	TArray<int32> MachineImageID;
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