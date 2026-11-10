// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Tickable.h"
#include "BusinessTime.generated.h"

class AActor;
class ADirectionalLight;
class UDirectionalLightComponent;

/** 季节：按 MonthsPerSeason 个月一季循环，默认 1~3 春、4~6 夏、7~9 秋、10~12 冬 */
UENUM(BlueprintType)
enum class ESeason : uint8
{
	Spring UMETA(DisplayName = "春"),
	Summer UMETA(DisplayName = "夏"),
	Autumn UMETA(DisplayName = "秋"),
	Winter UMETA(DisplayName = "冬"),
};

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time")
	int32 StartYear = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time")
	int32 StartMonth = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time")
	int32 StartDay = 1;

	/** 每月天数（用于月末翻月） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time", meta = (ClampMin = "1", ClampMax = "31"))
	int32 DaysPerMonth = 30;

	/** 一天的“营业日”起止小时：DayProgress=0 映射 DayStartHour，=1 映射 DayEndHour（如 8~22），而非 0~24 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time", meta = (ClampMin = "0", ClampMax = "24"))
	float DayStartHour = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time", meta = (ClampMin = "0", ClampMax = "24"))
	float DayEndHour = 22.f;

	/** 每个游戏日对应的真实秒数（越小时间流逝越快；<=0 则停止推进） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time", meta = (ClampMin = "0.1"))
	float RealSecondsPerDay = 100.f;

	/** 是否自动推进（关闭后需手动调用 AdvanceDay） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time")
	bool bAutoAdvance = true;

	// ========== 季节（由月份换算，驱动野外采集物按季刷新） ==========
	/** 每季包含的月数（默认 3 个月一季） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Season", meta = (ClampMin = "1", ClampMax = "12"))
	int32 MonthsPerSeason = 3;

	/** 当前季节（只读；随日期推进自动更新） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Time|Season")
	ESeason CurrentSeason = ESeason::Spring;

	/** 季节切换广播（参数：旧季节、新季节），供野外采集 / 环境 / UI 监听 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSeasonChanged, ESeason, OldSeason, ESeason, NewSeason);

	UPROPERTY(BlueprintAssignable, Category = "Shoot|Time|Season")
	FOnSeasonChanged OnSeasonChanged;

	/** 当前季节（按当前月份实时换算，读档后也不会滞后） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Time|Season")
	ESeason GetSeason() const { return SeasonFromMonth(Month, MonthsPerSeason); }

	/** 当前季节名（春 / 夏 / 秋 / 冬），供 UI 显示 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Time|Season")
	FString GetSeasonName() const;

	/** 由月份换算季节（月份从 1 起；PerSeason 个月一季，超出部分按 4 季循环） */
	static ESeason SeasonFromMonth(int32 InMonth, int32 InMonthsPerSeason);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time")
	bool bShowDebugInfo = false;

	// ========== 营业时段（小时制：与 DayProgress 映射的 DayStartHour~DayEndHour 同一坐标系） ==========
	/** 可开始营业的小时；自动营业关闭时仍可由玩家到大门按 E 开门 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Business")
	float OpenHour = 18.f;

	/** 自动打烊小时（默认 22 = 晚上 10 点）：到点自动结束营业（灯灭、清场） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Business")
	float CloseHour = 22.f;

	/** 默认自动营业；当日手动打烊后不再自动重开，旧日客人收尾完成后才允许自动开门。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Business")
	bool bAutoOpenBusiness = true;

	/** 当前营业状态（运行时）：自动或手动开门且尚未打烊 */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Time|Business")
	bool bIsBusinessOpen = false;

	/** 营业状态变化广播（true=开门营业，false=打烊），供灯饰 / 客人系统 / UI 监听 */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Time|Business")
	FOnBusinessOpenChanged OnBusinessOpenChanged;

	/** 当前日期（只读） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Time")
	int32 Year = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Time")
	int32 Month = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Time")
	int32 Day = 1;

	/** 当天进度 0~1（用于 UI 进度条） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Time")
	float DayProgress = 0.f;

	/** 经营时钟：由 DayProgress 映射，日期 / 营业 / UI 始终使用真实经营小时。 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Time")
	float GetCurrentHour() const { return DayStartHour + DayProgress * (DayEndHour - DayStartHour); }

	// ========== 营业视觉（不改变经营时钟或游戏日长度） ==========
	/** 营业及打烊收尾期间锁住视觉小时；关闭后恢复原来的实时昼夜 / 天气表现。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Lighting")
	bool bStabilizeBusinessLighting = true;

	/** 营业 / 收尾期间固定的视觉小时（默认正午，阴影短且静止）；经营时钟照常走。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Lighting", meta = (ClampMin = "0", ClampMax = "24"))
	float BusinessStartVisualHour = 12.f;

	/** 开门时视觉小时顺时针过渡到 BusinessStartVisualHour 的速度（游戏小时 / 真实秒）；0 为瞬切。
	 *  过渡只在开门最初几秒（首位客人到店前）完成，之后整段营业光影静止。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Lighting", meta = (ClampMin = "0"))
	float BusinessFocusTransitionHoursPerSecond = 3.f;

	/** 收尾结束后顺时针追赶经营小时的速度（游戏小时 / 真实秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Lighting", meta = (ClampMin = "0"))
	float VisualRecoveryHoursPerSecond = 1.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Shoot|Time|Lighting")
	float VisualHour = 8.f;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Time|Lighting")
	float GetVisualHour() const { return bStabilizeBusinessLighting ? VisualHour : GetCurrentHour(); }

	/** 稳光开启且正在营业或任意 GuestManager 尚在收尾；不依赖 UObject 默认 GetWorld。 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Time|Lighting")
	bool IsBusinessVisualFocusActive() const;

	/** 当前是否处于可营业时段（OpenHour <= 小时 < CloseHour；假设不跨午夜） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Time|Business")
	bool IsWithinBusinessHours() const;

	/** 是否已营业（自动或手动开门且尚未打烊） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Time|Business")
	bool IsBusinessOpen() const { return bIsBusinessOpen; }

	/** 显式开门清除当日自动开门抑制；成功返回 true（已营业也返回 true），时段外返回 false。 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Time|Business")
	bool OpenBusiness();

	/** 手动打烊（玩家可提前关门，或夜间自动打烊时调用） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Time|Business")
	void CloseBusiness();

	// ========== 太阳（DirectionalLight）随一天时间变化 ==========
	/** 要随日出日落驱动的平行光（太阳）。为空时由关卡在 BeginPlay 自动按标签"Sun"或首个 DirectionalLight 绑定 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Sun")
	TObjectPtr<ADirectionalLight> SunLight;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Shoot|Time|Sun")
	TObjectPtr<AActor> StylizedSkyActor;

	/** 是否自动驱动太阳（角度 / 颜色 / 强度随 GetVisualHour 变化）；关闭保留原太阳行为 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Sun")
	bool bDriveSun = true;

	/** 正午太阳强度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Sun", meta = (ClampMin = "0"))
	float NoonIntensity = 3.f;
	/** 夜晚（日落后）太阳强度（很低，靠灯饰补光） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Sun", meta = (ClampMin = "0"))
	float NightIntensity = 0.05f;

	/** 正午太阳颜色（偏白） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Sun")
	FLinearColor NoonColor = FLinearColor(1.f, 0.98f, 0.92f);
	/** 日出 / 日落颜色（偏暖橙） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Sun")
	FLinearColor SunsetColor = FLinearColor(1.f, 0.55f, 0.2f);
	/** 夜晚颜色（偏冷蓝） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Sun")
	FLinearColor NightColor = FLinearColor(0.2f, 0.3f, 0.6f);

	/** 天气对太阳的强度倍率（由 AWeatherSystem 写入，UpdateSun 里与日变化强度相乘） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Time|Sun")
	float WeatherSunIntensityScale = 1.f;

	/** 天气对太阳颜色的染色（与日变化颜色相乘，晴天为白色） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Time|Sun")
	FLinearColor WeatherSunColorTint = FLinearColor::White;

	/** 天气系统每帧写入太阳修正（强度倍率 + 颜色染色） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Time|Sun")
	void SetWeatherSunModifiers(float InIntensityScale, FLinearColor InColorTint);

	/** 日出小时（默认 6 点）、日落小时（默认 18 点）：两者之间为白天，之外为夜晚 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Sun")
	float SunriseHour = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Time|Sun")
	float SunsetHour = 18.f;

	/** 根据 GetVisualHour 更新太阳方向 / 颜色 / 强度（SunLight 为空或 bDriveSun=false 时跳过） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Time|Sun")
	void UpdateSun();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDateChanged, int32, Year, int32, Month, int32, Day);

	/** 每过一天触发（年/月变化当天也会先触发本事件，再触发 OnMonth/OnYear） */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Time")
	FOnDateChanged OnDayChanged;
	/** 跨月时触发（含跨年那次） */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Time")
	FOnDateChanged OnMonthChanged;
	/** 跨年时触发 */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Time")
	FOnDateChanged OnYearChanged;

	/** 初始化起始日期并清零累计器（由 UBusinessSimulation::Init 调用） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Time")
	void Init();

	/** 暂停 / 恢复走时 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Time")
	void SetPaused(bool bPaused) { bPausedState = bPaused; }

	/** 手动推进 n 天（调试 / 暂停态下使用） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Time")
	void AdvanceDays(int32 Days);

	// ---- FTickableGameObject ----
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UTimeSystem, STATGROUP_Tickables);
	}

private:
	void AdvanceOneDay(bool bResetDayProgress = false);
	void UpdateBusinessState();
	void UpdateVisualHour(float DeltaTime);
	bool HasPendingClosingGuests() const;

	double Accum = 0.0;
	bool bPausedState = false;
	bool bBusinessClosedForDay = false;

	// 用连续小时追赶，不每帧从两个已回绕的 float 推断方向，避免追平后误判为再转一整圈。
	double VisualRecoveryHour = 0.0;
	double VisualRecoveryTargetHour = 0.0;
	double LastVisualClockHour = 0.0;
	bool bVisualRecoveryInitialized = false;
};
