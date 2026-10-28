// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Tickable.h"
#include "BusinessTime.generated.h"

class ADirectionalLight;
class UDirectionalLightComponent;

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
	/** 营业状态变化委托（true=开门营业，false=打烊）：须在 OnBusinessOpenChanged 属性前声明 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBusinessOpenChanged, bool, bOpen);

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

	/** 一天的“营业日”起止小时：DayProgress=0 映射 DayStartHour，=1 映射 DayEndHour（如 8~22），而非 0~24 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (ClampMin = "0", ClampMax = "24"))
	float DayStartHour = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (ClampMin = "0", ClampMax = "24"))
	float DayEndHour = 22.f;

	/** 每个游戏日对应的真实秒数（越小时间流逝越快；<=0 则停止推进） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (ClampMin = "0.1"))
	float RealSecondsPerDay = 100.f;

	/** 是否自动推进（关闭后需手动调用 AdvanceDay） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time")
	bool bAutoAdvance = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time")
	bool bShowDebugInfo = false;

	// ========== 营业时段（小时制：与 DayProgress 映射的 DayStartHour~DayEndHour 同一坐标系） ==========
	/** 可开始营业的小时（默认 8 = 早上 8 点；玩家到大门按 E 开门后才真正营业） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time|Business")
	float OpenHour = 18.f;

	/** 自动打烊小时（默认 22 = 晚上 10 点）：到点自动结束营业（灯灭、清场） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time|Business")
	float CloseHour = 22.f;

	/** 是否自动营业：开启后进入营业时段（OpenHour）即自动开门，无需玩家到大门按 E 开门。
	 *  默认关闭——餐厅默认“打烊/备货、灯灭”，需玩家到大门按 E 开门才真正营业并点亮营业灯饰。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time|Business")
	bool bAutoOpenBusiness = false;

	/** 当前营业状态（运行时）：玩家已开门且仍处于营业时段内才为 true */
	UPROPERTY(BlueprintReadOnly, Category = "Time|Business")
	bool bIsBusinessOpen = false;

	/** 营业状态变化广播（true=开门营业，false=打烊），供灯饰 / 客人系统 / UI 监听 */
	UPROPERTY(BlueprintAssignable, Category = "Time|Business")
	FOnBusinessOpenChanged OnBusinessOpenChanged;

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

	/** 当前一天内的小时（落在 DayStartHour~DayEndHour，默认 8~22），由 DayProgress 映射；营业 / 太阳都以此为准 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Time")
	float GetCurrentHour() const { return DayStartHour + DayProgress * (DayEndHour - DayStartHour); }

	/** 当前是否处于可营业时段（OpenHour <= 小时 < CloseHour；假设不跨午夜） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Time|Business")
	bool IsWithinBusinessHours() const;

	/** 是否已营业（玩家已开门且仍在营业时段内） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Time|Business")
	bool IsBusinessOpen() const { return bIsBusinessOpen; }

	/** 玩家到大门按 E 开门营业：成功返回 true（已营业也返回 true），未到营业时段返回 false */
	UFUNCTION(BlueprintCallable, Category = "Time|Business")
	bool OpenBusiness();

	/** 手动打烊（玩家可提前关门，或夜间自动打烊时调用） */
	UFUNCTION(BlueprintCallable, Category = "Time|Business")
	void CloseBusiness();

	// ========== 太阳（DirectionalLight）随一天时间变化 ==========
	/** 要随日出日落驱动的平行光（太阳）。为空时由关卡在 BeginPlay 自动按标签"Sun"或首个 DirectionalLight 绑定 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time|Sun")
	TObjectPtr<ADirectionalLight> SunLight;

	/** 是否自动驱动太阳（角度 / 颜色 / 强度随 DayProgress 变化）；关闭则只走日期 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time|Sun")
	bool bDriveSun = true;

	/** 正午太阳强度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time|Sun", meta = (ClampMin = "0"))
	float NoonIntensity = 3.f;
	/** 夜晚（日落后）太阳强度（很低，靠灯饰补光） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time|Sun", meta = (ClampMin = "0"))
	float NightIntensity = 0.05f;

	/** 正午太阳颜色（偏白） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time|Sun")
	FLinearColor NoonColor = FLinearColor(1.f, 0.98f, 0.92f);
	/** 日出 / 日落颜色（偏暖橙） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time|Sun")
	FLinearColor SunsetColor = FLinearColor(1.f, 0.55f, 0.2f);
	/** 夜晚颜色（偏冷蓝） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time|Sun")
	FLinearColor NightColor = FLinearColor(0.2f, 0.3f, 0.6f);

	/** 日出小时（默认 6 点）、日落小时（默认 18 点）：两者之间为白天，之外为夜晚 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time|Sun")
	float SunriseHour = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time|Sun")
	float SunsetHour = 18.f;

	/** 根据当前 DayProgress 更新太阳方向 / 颜色 / 强度（SunLight 为空或 bDriveSun=false 时跳过） */
	UFUNCTION(BlueprintCallable, Category = "Time|Sun")
	void UpdateSun();

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
