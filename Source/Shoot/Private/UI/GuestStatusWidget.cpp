// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/GuestStatusWidget.h"
#include "Engine/DataTable.h"
#include "TimerManager.h"

void UGuestStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 气泡常显：整体不透明度固定为满值，不做呼吸渐隐渐显（显隐由 AGuestCharacter 按状态控制）。
	// 这里显式写一次，盖掉蓝图里可能残留的不透明度/动画设定。
	SetRenderOpacity(1.0f);

	// 周期性随机抽一句台词通知蓝图切换（每位客人间隔不同，且每次重新随机）。
	if (bAutoSwitchLine)
	{
		ScheduleLineSwitch();
	}
}

void UGuestStatusWidget::ScheduleLineSwitch()
{
	const float MinS = FMath::Min(LineSwitchMin, LineSwitchMax);
	const float MaxS = FMath::Max(LineSwitchMin, LineSwitchMax);
	const float Interval = (MaxS > MinS) ? FMath::RandRange(MinS, MaxS) : LineSwitchMin;
	if (Interval <= 0.f)
	{
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(
		LineSwitchTimerHandle, this, &UGuestStatusWidget::TriggerLineSwitch, Interval, false);
}

void UGuestStatusWidget::TriggerLineSwitch()
{
	// 通知蓝图重新抽一句（蓝图读取 OwnerCharacter 当前状态，刷新文本/图标）。
	OnSwitchLine();

	// 重新随机下一次间隔，避免所有客人整齐划一地换话。
	ScheduleLineSwitch();
}
