// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/BusinessTime.h"
#include "Engine/Engine.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"

void UTimeSystem::Init()
{
	Year = FMath::Max(1, StartYear);
	Month = FMath::Clamp(StartMonth, 1, 12);
	Day = FMath::Clamp(StartDay, 1, DaysPerMonth);
	Accum = 0.f;
	DayProgress = 0.f;
	// 运行时营业状态必须随初始化清零，避免“启动即营业”（CDO 残留 / 旧状态 / 异常广播造成的脏值）
	bIsBusinessOpen = false;
	UpdateSun();
}

void UTimeSystem::AdvanceDays(int32 Days)
{
	Days = FMath::Max(0, Days);
	for (int32 i = 0; i < Days; ++i)
	{
		AdvanceOneDay();
	}
}

void UTimeSystem::AdvanceOneDay()
{
	const int32 PrevMonth = Month;
	const int32 PrevYear = Year;

	Day++;
	if (Day > DaysPerMonth)
	{
		Day = 1;
		Month++;
		if (Month > 12)
		{
			Month = 1;
			Year++;
		}
	}

	OnDayChanged.Broadcast(Year, Month, Day);
	if (Month != PrevMonth)
	{
		OnMonthChanged.Broadcast(Year, Month, Day);
	}
	if (Year != PrevYear)
	{
		OnYearChanged.Broadcast(Year, Month, Day);
	}
}

void UTimeSystem::Tick(float DeltaTime)
{
	if (bPausedState || !bAutoAdvance || RealSecondsPerDay <= 0.f)
	{
		return;
	}

	Accum += DeltaTime;
	while (Accum >= RealSecondsPerDay)
	{
		Accum -= RealSecondsPerDay;
		AdvanceOneDay();
	}
	DayProgress = FMath::Clamp(Accum / RealSecondsPerDay, 0.f, 1.f);

	// 太阳随一天时间变化（角度 / 颜色 / 强度）
	UpdateSun();

	if (bShowDebugInfo && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 0.2f, FColor::White,
			FString::Printf(TEXT("[Time] %d-%02d-%02d (%.0f%%)"), Year, Month, Day, DayProgress * 100.f));
	}

	// 自动打烊：营业中且已过 CloseHour（如晚上 10 点），到点结束营业（灯灭、清场由监听方处理）
	if (bIsBusinessOpen && GetCurrentHour() >= CloseHour)
	{
		CloseBusiness();
	}

	// 自动营业：开启 bAutoOpenBusiness 且尚未营业且当前处于营业时段（OpenHour~CloseHour），
	// 则自动开门（无需玩家到大门按 E）。OpenBusiness 内部幂等，已营业时不会重复广播。
	if (bAutoOpenBusiness && !bIsBusinessOpen && IsWithinBusinessHours())
	{
		OpenBusiness();
	}
}

bool UTimeSystem::IsWithinBusinessHours() const
{
	const float h = GetCurrentHour();
	return h >= OpenHour && h < CloseHour;
}

bool UTimeSystem::OpenBusiness()
{
	if (bIsBusinessOpen)
	{
		return true; // 已营业，幂等
	}
	if (!IsWithinBusinessHours())
	{
		// 诊断：未在营业时段仍被要求开门（如白天备货时段），拒绝
		UE_LOG(LogTemp, Warning, TEXT("[TimeSystem] OpenBusiness 被拒绝：当前小时=%.2f 不在 OpenHour=%.1f~CloseHour=%.1f"),
			GetCurrentHour(), OpenHour, CloseHour);
		return false; // 未到营业时间（如白天备货时段），拒绝开门
	}
	bIsBusinessOpen = true;
	UE_LOG(LogTemp, Warning, TEXT("[TimeSystem] >>> 开门营业！当前小时=%.2f（调用来源可能是玩家按 E 或蓝图）"), GetCurrentHour());
	OnBusinessOpenChanged.Broadcast(true);
	return true;
}

void UTimeSystem::CloseBusiness()
{
	if (!bIsBusinessOpen)
	{
		return;
	}
	bIsBusinessOpen = false;
	UE_LOG(LogTemp, Warning, TEXT("[TimeSystem] <<< 打烊（CloseBusiness）"));
	OnBusinessOpenChanged.Broadcast(false);
}

void UTimeSystem::UpdateSun()
{
	if (!bDriveSun || !SunLight)
	{
		return;
	}

	UDirectionalLightComponent* DLC = Cast<UDirectionalLightComponent>(SunLight->GetLightComponent());
	if (!DLC)
	{
		return;
	}

	const float Hour = GetCurrentHour();
	const bool bIsDay = Hour > SunriseHour && Hour < SunsetHour;

	// 太阳高度系数：白天为 sin 曲线（0 在地平线，1 在正午），夜晚 < 0（在地平线下）
	float Height01 = 0.f;
	if (bIsDay)
	{
		const float DayLen = FMath::Max(0.001f, SunsetHour - SunriseHour);
		const float x = (Hour - SunriseHour) / DayLen; // 0..1
		Height01 = FMath::Sin(x * PI);                  // 0..1..0
	}
	else
	{
		Height01 = -1.f;
	}

	// 强度：夜晚取 NightIntensity，白天在 Night~Noon 之间随高度过渡
	const float Intensity = bIsDay
		? FMath::GetMappedRangeValueClamped(FVector2D(0.f, 1.f), FVector2D(NightIntensity, NoonIntensity), Height01)
		: NightIntensity;
	DLC->SetIntensity(Intensity);

	// 颜色：夜晚 NightColor；白天由 SunsetColor（地平线暖橙）到 NoonColor（正午偏白）过渡
	const FLinearColor Color = bIsDay
		? FLinearColor::LerpUsingHSV(SunsetColor, NoonColor, FMath::Clamp(Height01, 0.f, 1.f))
		: NightColor;
	DLC->SetLightColor(Color);

	// 方向：太阳在天空中的位置（Z 为上）。白天按高度 / 方位插值，夜晚压到很低角度（强度已近 0，仅占位）
	const float ElevDeg = FMath::Clamp(Height01, 0.f, 1.f) * 90.f; // 0~90°
	const float x = bIsDay ? (Hour - SunriseHour) / FMath::Max(0.001f, SunsetHour - SunriseHour) : 0.5f;
	const float AzimDeg = FMath::GetMappedRangeValueClamped(FVector2D(0.f, 1.f), FVector2D(-90.f, 90.f), x);
	const float ElevRad = FMath::DegreesToRadians(ElevDeg);
	const float AzimRad = FMath::DegreesToRadians(AzimDeg);
	const FVector ToSun = FVector(
		FMath::Cos(ElevRad) * FMath::Sin(AzimRad),
		FMath::Cos(ElevRad) * FMath::Cos(AzimRad),
		FMath::Sin(ElevRad)
	).GetSafeNormal();
	// 平行光没有 SetDirection：其光照沿组件本地 +X 轴（前进方向）传播，太阳在天上则光朝 -ToSun 照射，
	// 故用旋转让组件 X 轴对齐传播方向（-ToSun）。
	DLC->SetWorldRotation(FRotationMatrix::MakeFromX(-ToSun).Rotator());
}
