// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Game/StaffSystem.h"
#include "MySaveGame.generated.h"

/**
 * 
 */
UCLASS()
class SHOOT_API UMySaveGame : public USaveGame
{
	GENERATED_BODY()
	public:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Basic)
	FString PlayerName;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Basic)
	int Money; // 玩家金钱
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Basic)
	int Reputation = 0; // 玩家声誉
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Basic)
	TMap<int,int> LevelMaxScore; // 关卡最高得分 <关卡,最大分数>
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Basic)
	TArray<int> Achievement;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Basic)
	TMap<int,int> PropMap; // 道具表 , <ID,数量>

	// —— 开罗式经营模拟（UBusinessSimulation）持久化字段 ——
	/** 游戏内日期（年 / 月 / 日） */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Business")
	int32 BizYear = 1;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Business")
	int32 BizMonth = 1;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Business")
	int32 BizDay = 1;

	/** 员工 / 居民名册（含属性与是否在职），替代"关卡制"后的核心经营资产 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Business")
	TArray<FStaffMember> BizRoster;

	/** 已解锁的食材 ID 列表（关卡达成目标后解锁场景上预先放置的食材，如玉米田里的玉米）。跨存档持久化。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Ingredient")
	TArray<int32> UnlockedIngredients;

	UMySaveGame();
};
