// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Item/ItemActor.h"
#include "Dish.generated.h"

/**
 * 碟子（原 EIT_Dish）：装菜的盘子。
 * 上菜成功时调用 OnRemoveActorEvent() 通知盘子系统移除一个碟子（C++ 内部找到 APlateSystem 调用 RemovePlate）。
 * OnRemoveActorEvent 委托/方法从原 AItemActor 抽取到本类。
 *
 * 注：空碟 / 脏碟（方案B 用餐闭环 + 洗碗机回收）的通用能力已上移到基类 AItemActor，
 * 因此任何成品（无论是否为 ADish）都能经历：上菜 → 桌上 → 吃完变空碟 → 玩家收走 → 交还洗碗机。
 */
UCLASS()
class SHOOT_API ADish : public AItemActor
{
	GENERATED_BODY()

public:
	/** 移除碟子时广播（蓝图可监听做反馈，沿用原 AItemActor 委托名） */
	UPROPERTY(BlueprintAssignable, Category = "Dish")
	FRemoveActorEvent FRemoveActorEvent;

	/** 触发生碟子移除逻辑（保持原 AItemActor::OnRemoveActorEvent 行为） */
	UFUNCTION(BlueprintCallable, Category = "Dish")
	void OnRemoveActorEvent();
};
