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
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Shoot|Basic")
	FString PlayerName;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Shoot|Basic")
	int Money; // 玩家金钱
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Shoot|Basic")
	int Reputation = 0; // 玩家声誉
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Shoot|Basic")
	TMap<int,int> LevelMaxScore; // 关卡最高得分 <关卡,最大分数>
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Shoot|Basic")
	TArray<int> Achievement;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Shoot|Basic")
	TMap<int,int> PropMap; // 道具表 , <ID,数量>

	// —— 开罗式经营模拟（UBusinessSimulation）持久化字段 ——
	/** 游戏内日期（年 / 月 / 日） */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Shoot|Business")
	int32 BizYear = 1;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Shoot|Business")
	int32 BizMonth = 1;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Shoot|Business")
	int32 BizDay = 1;

	/** 员工 / 居民名册（含属性与是否在职），替代"关卡制"后的核心经营资产 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Shoot|Business")
	TArray<FStaffMember> BizRoster;

	/** 已解锁的食材 ID 列表（关卡达成目标后解锁场景上预先放置的食材，如玉米田里的玉米）。跨存档持久化。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Shoot|Ingredient")
	TArray<int32> UnlockedIngredients;

	/** 食物箱剩余数量持久化：键 = 箱子 ActorGuid 字符串，值 = 剩余可生成数量。跨存档持久化。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Shoot|Spawner")
	TMap<FString, int32> SpawnerRemaining;

	/** 野外植物冷却快照（秒）；0 表示可采，不计算游戏关闭期间的时间。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, SaveGame, Category = "Shoot|Plant")
	TMap<FString, float> WildPlantRegrowRemaining;

	/** 已发现过的物品键（DT 行名，如 "7"）：用于首次发现弹窗与奖励只触发一次。跨存档持久化。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Shoot|Discovery")
	TArray<FName> DiscoveredItemKeys;

	UMySaveGame();
};
