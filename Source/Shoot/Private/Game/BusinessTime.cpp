// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/BusinessTime.h"
#include "Engine/Engine.h"

void UTimeSystem::Init()
{
	Year = FMath::Max(1, StartYear);
	Month = FMath::Clamp(StartMonth, 1, 12);
	Day = FMath::Clamp(StartDay, 1, DaysPerMonth);
	Accum = 0.f;
	DayProgress = 0.f;
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

	if (bShowDebugInfo && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 0.2f, FColor::White,
			FString::Printf(TEXT("[Time] %d-%02d-%02d (%.0f%%)"), Year, Month, Day, DayProgress * 100.f));
	}
}
