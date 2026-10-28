// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Item/ItemActor.h"
#include "Desk.generated.h"

/**
 * 上菜台 / 餐桌（原 EIT_Desk）：角色不能拾取它；客人上菜时目标须为 Desk。
 * 作为独立类型标记类从 AItemActor 抽取出来（取代原枚举分支判断）。
 */
UCLASS()
class SHOOT_API ADesk : public AItemActor
{
	GENERATED_BODY()
};
