// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Item/ItemActor.h"
#include "FoodSpawnOther.generated.h"

/**
 * 切菜生成物（原 EIT_SpawnOther）：生/半成品食材，经切菜后按 SpawnItemActorClass 生成“切好”的版本。
 * SpawnItemActorClass 继承自基类 AItemActor（与 AFoodSpawner 共用），本类主要提供独立类型身份。
 */
UCLASS()
class SHOOT_API AFoodSpawnOther : public AItemActor
{
	GENERATED_BODY()
};
