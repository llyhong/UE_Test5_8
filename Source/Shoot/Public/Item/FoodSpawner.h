// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Item/ItemActor.h"
#include "FoodSpawner.generated.h"

/**
 * 生成食物的箱子：从 AItemActor 中抽取出来的独立类（原 EIT_Spawn 类型）。
 * 玩家拾取（按 E）此箱子时，会依据 SpawnItemActorClass 生成一个食材/食物到手上。
 * SpawnItemActorClass 仍保留在基类 AItemActor 上（切菜生成 EIT_SpawnOther 也复用），
 * 本类主要提供“生成箱”的独立类型身份与 OnSpawnActor 蓝图事件。
 */
UCLASS()
class SHOOT_API AFoodSpawner : public AItemActor
{
	GENERATED_BODY()

public:
	/**
	 * 箱内剩余可生成数量：
	 *  -1 表示无限生成（默认，保持原行为）；
	 *   0 表示已空，不再生成任何食材；
	 *  >0 表示剩余数量，每次拾取生成后减一，减到 0 即停。
	 * 在蓝图实例上按食材种类设置（如番茄箱设 5）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	int32 RemainingCount = -1;

	/** 是否还能生成（无限或剩余>0） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Spawn")
	bool CanSpawn() const { return RemainingCount != 0; }

	/** 成功生成一次后消耗一个数量（无限模式不影响），并写回存档 */
	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void ConsumeOne();

	/** 关卡加载时从存档恢复剩余数量（首次则写入初始默认值） */
	virtual void BeginPlay() override;

	/** 触发生成时广播（蓝图可挂特效/音效，或自行处理生成相关逻辑） */
	UPROPERTY(BlueprintAssignable, Category = "Spawn")
	FSpawnActorEvent OnSpawnActor;

	/** 触发生成广播（保持原 AItemActor::OnSpawnActorEvent 的行为） */
	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void OnSpawnActorEvent();
};
