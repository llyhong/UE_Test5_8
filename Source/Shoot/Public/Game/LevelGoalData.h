// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "LevelGoalData.generated.h"

/**
 * 关卡目标（通过条件）数据表行结构（DataTable: LevelGoals，RowStruct = FLevelGoalRow）。
 * 在编辑器中创建一张 DataTable（行结构选 FLevelGoalRow），按关卡 LevelID 配置通过条件。
 * 任一目标字段为 0 表示该维度不要求；全部要求的维度达成即视为“通过本关”，
 * 并解锁 UnlockIngredientIDs 中列出的食材（场景上预先放置的食材，如玉米田里的玉米）。
 */
USTRUCT(BlueprintType)
struct FLevelGoalRow : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 关卡 ID（与 ABaseLevelBP::LevelID 对应）。行名建议也用该 ID，便于查找 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelGoal")
	int32 LevelID = 0;

	/** 需要达到的分数（0 = 不要求） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelGoal")
	int32 TargetScore = 0;

	/** 本关累计赚取的钱（仅计正向收入，0 = 不要求） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelGoal")
	int32 TargetMoneyEarned = 0;

	/** 本关累计服务的客人数量（完成订单数，0 = 不要求） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelGoal")
	int32 TargetGuestsServed = 0;

	/** 需要达到的声誉值（0 = 不要求） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelGoal")
	int32 TargetReputation = 0;

	/** 达成目标后解锁的食材 ID 列表（对应 AItemActor::IngredientUnlockID；<0 的 ID 视为无需解锁） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelGoal")
	TArray<int32> UnlockIngredientIDs;

	/** 目标描述（UI 展示，例如“赚取 100 金币并服务 10 位客人”） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelGoal")
	FText GoalDescription;

	// ===== 以下字段由原 FLevelInfo 合并而来（关卡入口 / 关卡选择信息） =====
	/** 关卡地图路径（点击关卡入口后 OpenLevel 用，如 "/Game/Maps/Level_01"） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelInfo")
	FString LevelPath;

	/** 关卡是否启用（关卡选择界面用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelInfo")
	bool IsActive = false;

	/** 关卡描述文本（关卡选择界面展示） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelInfo")
	FString LevelDes;

	/** 关卡选择界面的背景图 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelInfo")
	TObjectPtr<UTexture2D> BackgroundTexture;

	/**
	 * 旧“星级过关”字段（Star0~Star3），已废弃。
	 * 经营成败现由 UBusinessSimulation::RestaurantStars（0~5 星，由声誉+服务品质计算）判定。
	 * 仅保留以兼容旧数据表，新项目请勿依赖。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelInfo|Deprecated")
	int32 Star0 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelInfo|Deprecated")
	int32 Star1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelInfo|Deprecated")
	int32 Star2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LevelInfo|Deprecated")
	int32 Star3 = 0;
};
