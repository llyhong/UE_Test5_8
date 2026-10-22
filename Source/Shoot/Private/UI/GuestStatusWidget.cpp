// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/GuestStatusWidget.h"
#include "Engine/DataTable.h"
#include "TimerManager.h"

void UGuestStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 聊天气泡式呼吸：周期性按正弦调节整体不透明度（渐隐渐显），避免一直满亮度常显。
	if (bAutoFade)
	{
		// 每位客人随机一个呼吸周期，避免所有气泡同步闪烁。
		const float MinP = FMath::Min(FadePeriodMin, FadePeriodMax);
		const float MaxP = FMath::Max(FadePeriodMin, FadePeriodMax);
		EffectiveFadePeriod = (MaxP > MinP) ? FMath::RandRange(MinP, MaxP) : FadePeriod;
		if (EffectiveFadePeriod <= 0.f)
		{
			EffectiveFadePeriod = FadePeriod;
		}

		FadeElapsed = 0.f;
		SetRenderOpacity(MaxOpacity);
		GetWorld()->GetTimerManager().SetTimer(
			FadeTimerHandle, this, &UGuestStatusWidget::TickFade, 0.05f, true);
	}

	// 周期性随机抽一句台词通知蓝图切换（每位客人间隔不同，且每次重新随机）。
	if (bAutoSwitchLine)
	{
		ScheduleLineSwitch();
	}
}

void UGuestStatusWidget::TickFade()
{
	FadeElapsed += 0.05f;
	const float Phase = FMath::Fmod(FadeElapsed, EffectiveFadePeriod) / EffectiveFadePeriod;
	// 0.5 + 0.5*cos 得到 1→0→1 的平滑曲线：渐隐后再渐显，循环往复。
	const float O = MinOpacity + (MaxOpacity - MinOpacity) * (0.5f + 0.5f * FMath::Cos(2.f * PI * Phase));
	SetRenderOpacity(O);
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
