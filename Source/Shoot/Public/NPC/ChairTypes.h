// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ChairTypes.generated.h"

/** 餐桌当前状态（供 TipsWidget 决定显示什么文案 / 图标） */
UENUM(BlueprintType)
enum class EChairTipsState : uint8
{
	/** 没客人、桌上也没有碟子 */
	Empty		UMETA(DisplayName = "空闲"),
	/** 客人已入座但还没轮到点单（入座过程中） */
	Seated		UMETA(DisplayName = "客人入座中"),
	/** 客人坐好，等待玩家按 E 下单 */
	WaitingOrder UMETA(DisplayName = "等待点单"),
	/** 已下单（座位绑定了菜单），等待玩家上菜 */
	WaitingFood	UMETA(DisplayName = "等待上菜"),
	/** 已上菜，客人正在用餐 */
	Eating		UMETA(DisplayName = "用餐中"),
	/** 客人已走，桌上留着待回收的空碟 */
	DirtyPlate	UMETA(DisplayName = "待收空碟")
};

/** 餐桌 TipsWidget（菜单提示控件）的显示时机 */
UENUM(BlueprintType)
enum class EChairSeatTipsMode : uint8
{
	/** 只有本座位绑定了菜单（客人已点单、等待上菜）时显示 —— 用于显示当前客人的菜单 */
	OrderOnly	UMETA(DisplayName = "有菜单时显示"),
	/** 只要有人（客人 / 空碟 / 菜单）就显示 */
	Always		UMETA(DisplayName = "有人时常显"),
	/** 仅玩家走进交互范围时显示（与机器类 TipsWidget 一致） */
	PlayerNear	UMETA(DisplayName = "玩家靠近时显示")
};
