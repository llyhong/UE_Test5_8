// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "TutorialTaskData.generated.h"

/** 新手任务类型：决定进度如何统计、箭头指向哪类物体 */
UENUM(BlueprintType)
enum class ETutorialTaskType : uint8
{
	/** 清理就餐区散落物（垃圾/积水）：进度 = 已清理数量 */
	CleanTrash UMETA(DisplayName = "清理垃圾"),
	/** 接待客人：进度 = 完成订单数（服务客人数） */
	ServeGuests UMETA(DisplayName = "接待客人"),
	/** 拾取某物品（如鸡蛋）：背包/手上拿到过就算 */
	PickupItem UMETA(DisplayName = "拾取物品"),
	/** 采摘某食材：bRequireBasket 区分“带篮子/不带篮子” */
	HarvestQuest UMETA(DisplayName = "采摘食材"),
	/** 把食材投入研究炉：进度 = 炉内该食材数量 */
	ResearchItem UMETA(DisplayName = "投入研究"),
	/** 把食材存进仓库/冰箱：进度 = 仓库内该食材数量 */
	StoreToWarehouse UMETA(DisplayName = "存入仓库"),
	/** 赚钱：进度 = 本关累计收入（LevelBP::MoneyEarnedThisLevel 按当天起点做差） */
	EarnMoney UMETA(DisplayName = "赚取金钱"),
};

/**
 * 新手任务数据表行结构（RowStruct = FTutorialTaskRow）。
 * 一天可以配多行，按 StepOrder 排序显示；同一天的任务同时开放，箭头指向第一个未完成的任务。
 * 目标物件通过 Actor 标签（TargetActorTag）指定，避免写死关卡坐标：
 *  - ResearchItem  → 标签打在研究炉上（留空则找任意 AResearchStove）
 *  - StoreToWarehouse → 标签打在冰箱/仓库上（留空则找任意 AStorageWarehouse）
 *  - HarvestQuest → 标签打在野外的植株/区域上（留空则找任意可采的目标作物）
 */
USTRUCT(BlueprintType)
struct FTutorialTaskRow : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 第几天（对应 UTimeSystem::Day，从 1 开始） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial")
	int32 Day = 1;

	/** 同一天内的显示顺序（小的在前） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial")
	int32 StepOrder = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial")
	ETutorialTaskType TaskType = ETutorialTaskType::CleanTrash;

	/** 需要完成的数量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial", meta = (ClampMin = "1"))
	int32 TargetCount = 1;

	/** 目标食材/物品 ID（DT_ItemInfo 行名对应的 ItemID；CleanTrash/ServeGuests 不需要） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial")
	int32 TargetItemID = -1;

	/** HarvestQuest 专用：true = 必须带着篮子采（进篮子），false = 空手采 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial")
	bool bRequireBasket = false;

	/** 目标物件标签：打在研究炉 / 冰箱仓库 / 植株区域等 Actor 上 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial")
	FName TargetActorTag = NAME_None;

	/** 任务列表文案，如“清理地板上的垃圾” */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial")
	FText Description;

	/** 箭头指引文案，如“去研究炉把番茄放进去” */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Tutorial")
	FText HintText;
};
