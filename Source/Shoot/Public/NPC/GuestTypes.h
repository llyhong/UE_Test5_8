// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GuestTypes.generated.h"

/** 客人状态机（抽到独立头文件，供 GuestCharacter 与 GuestStatusWidget 共用，避免循环依赖） */
UENUM(BlueprintType)
enum class EGuestState : uint8
{
	WalkingIn UMETA(DisplayName = "WalkingIn"),
	/** 排队等待中（没有空位）：供动画蓝图切换"等待"动画 */
	Waiting UMETA(DisplayName = "Waiting"),
	Seated UMETA(DisplayName = "Seated"),
	WaitingFood UMETA(DisplayName = "WaitingFood"),
	Eating UMETA(DisplayName = "Eating"),
	Leaving UMETA(DisplayName = "Leaving"),
	Dead UMETA(DisplayName = "Dead"),
	/** 落座但尚未被玩家接单（仅“需要玩家接单”模式）：头上提示“请按E点单”，玩家接单后转入 WaitingFood */
	WaitingOrder UMETA(DisplayName = "WaitingOrder"),
};
