// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "PlantInfo.generated.h"

class AItemActor;

/**
 * 单个生长阶段配置（可在 DataTable 中按数组顺序配置多个阶段，如：种子 -> 发芽 -> 结果）
 */
USTRUCT(BlueprintType)
struct FPlantGrowthStage
{
	GENERATED_BODY()

public:
	/** 阶段名（如：种子 / 发芽 / 结果），用于 UI 显示 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth")
	FName StageName = NAME_None;

	/** 该阶段所需的生长时间（秒），受 FPlantInfo::GrowthSpeed 影响 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth")
	float StageDuration = 5.f;

	/** 该阶段显示的模型（可直接在编辑器选择，留空则保持上一阶段模型） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth")
	TObjectPtr<UStaticMesh> StageMesh;

	/** 该阶段的缩放 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth")
	FVector StageScale = FVector::OneVector;
};

/**
 * 植物配置数据表行：浇水 / 驱虫 / 生长阶段 / 生长速度 / 果实 均通过此 Struct 配置
 * 在编辑器中使用 DataTable（RowType = "PlantInfo"）即可批量配置不同植物。
 */
USTRUCT(BlueprintType)
struct FPlantInfo : public FTableRowBase
{
	GENERATED_BODY()

public:
	// ========== 生长阶段（可配置，按数组顺序推进） ==========
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth")
	TArray<FPlantGrowthStage> GrowthStages;

	/** 生长速度倍率（>1 更快，<1 更慢） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth")
	float GrowthSpeed = 1.f;

	// ========== 浇水 ==========
	/** 是否需要浇水（缺水分时停止生长） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	bool bRequiresWater = true;

	/** 水分上限 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	float WaterMax = 100.f;

	/** 每秒消耗的水量（缺水分则停止生长） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	float WaterConsumeRate = 1.f;

	// ========== 驱虫 ==========
	/** 是否会生虫（生虫时停止生长，需要驱虫） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pest")
	bool bCanGetPest = true;

	/** 每隔多少秒可能生虫（<=0 表示不会随机生虫，只能手动测试/外部触发） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pest")
	float PestInterval = 30.f;

	// ========== 果实 ==========
	/** 结出的果实类型（在蓝图中选择 ClassType，建议为 AItemActor 子类以便拾取） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fruit")
	TSubclassOf<AItemActor> FruitClass = nullptr;

	/** 成熟时结出的果实数量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fruit")
	int32 FruitCount = 1;

	/** 果实挂点（植物模型上的插槽名）。多个果实时按 “基础名_索引” 自动对齐到对应插槽（如 Fruit_0 / Fruit_1 / Fruit_2…）；不足时回退到基础名。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fruit")
	FName FruitAttachSocket = NAME_None;

	/** 被玩家摘果后是否自动重新开始结果（循环生长，否则摘一次后停止） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fruit")
	bool bAutoRefruit = false;

	/** 自动重新结果时，每隔多少秒再长出一个果实（秒）。<=0 表示摘后立即一次性重新结满；>0 表示逐个生长（每隔该秒数长一个） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fruit")
	float RefruitInterval = 0.f;

	// ========== 熟度（结果阶段内的“成熟 / 熟过头”子阶段） ==========
	/** 是否启用熟度子阶段：结果后继续计时，区分“成熟”与“熟过头”，从而决定采摘品质 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ripeness")
	bool bEnableRipeness = true;

	/** 进入结果阶段后，经过多久（秒）达到“成熟”（此窗口内采摘 = 佳品） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ripeness", meta = (EditCondition = "bEnableRipeness", ClampMin = "0"))
	float RipeTime = 8.f;

	/** 进入结果阶段后，经过多久（秒）达到“熟过头”（此窗口后采摘 = 次品） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ripeness", meta = (EditCondition = "bEnableRipeness", ClampMin = "0"))
	float OverripeTime = 25.f;
};
