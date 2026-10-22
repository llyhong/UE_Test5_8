// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Tickable.h"
#include "BusinessTime.generated.h"

/**
 * 游戏内时钟：按"真实秒 / 游戏日"自动推进 年→月→日。
 * 用作开罗式经营模拟的时间轴，驱动 日/月/年 结算。
 * 作为 FTickableGameObject 实现，无需依赖具体关卡即可持续走时。
 */
UCLASS(Blueprintable)
class SHOOT_API UTimeSystem : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** 起始日期 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time")
	int32 StartYear = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time")
	int32 StartMonth = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time")
	int32 StartDay = 1;

	/** 每月天数（用于月末翻月） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (ClampMin = "1", ClampMax = "31"))
	int32 DaysPerMonth = 30;

	/** 每个游戏日对应的真实秒数（越小时间流逝越快；<=0 则停止推进） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (ClampMin = "0.1"))
	float RealSecondsPerDay = 20.f;

	/** 是否自动推进（关闭后需手动调用 AdvanceDay） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time")
	bool bAutoAdvance = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time")
	bool bShowDebugInfo = false;

	/** 当前日期（只读） */
	UPROPERTY(BlueprintReadOnly, Category = "Time")
	int32 Year = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Time")
	int32 Month = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Time")
	int32 Day = 1;

	/** 当天进度 0~1（用于 UI 进度条） */
	UPROPERTY(BlueprintReadOnly, Category = "Time")
	float DayProgress = 0.f;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDateChanged, int32, Year, int32, Month, int32, Day);

	/** 每过一天触发（年/月变化当天也会先触发本事件，再触发 OnMonth/OnYear） */
	UPROPERTY(BlueprintAssignable, Category = "Time")
	FOnDateChanged OnDayChanged;
	/** 跨月时触发（含跨年那次） */
	UPROPERTY(BlueprintAssignable, Category = "Time")
	FOnDateChanged OnMonthChanged;
	/** 跨年时触发 */
	UPROPERTY(BlueprintAssignable, Category = "Time")
	FOnDateChanged OnYearChanged;

	/** 初始化起始日期并清零累计器（由 UBusinessSimulation::Init 调用） */
	UFUNCTION(BlueprintCallable, Category = "Time")
	void Init();

	/** 暂停 / 恢复走时 */
	UFUNCTION(BlueprintCallable, Category = "Time")
	void SetPaused(bool bPaused) { bPausedState = bPaused; }

	/** 手动推进 n 天（调试 / 暂停态下使用） */
	UFUNCTION(BlueprintCallable, Category = "Time")
	void AdvanceDays(int32 Days);

	// ---- FTickableGameObject ----
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UTimeSystem, STATGROUP_Tickables);
	}

private:
	void AdvanceOneDay();
	float Accum = 0.f;
	bool bPausedState = false;
};
