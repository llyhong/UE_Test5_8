// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/StaffSystem.h"

int32 UStaffSystem::HireStaff(const FStaffMember& Candidate, int32 HireCost)
{
	FStaffMember NewMember = Candidate;
	NewMember.bHired = true;
	Roster.Add(NewMember);
	return Roster.Num() - 1;
}

bool UStaffSystem::TrainStaff(int32 Index, EStaffAttribute Attr, int32 TrainingCost)
{
	if (!Roster.IsValidIndex(Index))
	{
		return false;
	}
	FStaffMember& M = Roster[Index];
	switch (Attr)
	{
	case EStaffAttribute::Cooking:    M.Cooking++;    break;
	case EStaffAttribute::Service:    M.Service++;    break;
	case EStaffAttribute::Management: M.Management++; break;
	default: return false;
	}
	return true;
}

void UStaffSystem::FireStaff(int32 Index)
{
	if (Roster.IsValidIndex(Index))
	{
		Roster.RemoveAt(Index);
	}
}

int32 UStaffSystem::GetTotalMonthlyWages() const
{
	int32 Sum = 0;
	for (const FStaffMember& M : Roster)
	{
		if (M.bHired)
		{
			Sum += M.Wage;
		}
	}
	return Sum;
}

float UStaffSystem::GetServiceQuality() const
{
	int32 Sum = 0;
	for (const FStaffMember& M : Roster)
	{
		if (M.bHired)
		{
			Sum += M.Service;
		}
	}
	return static_cast<float>(Sum);
}

int32 UStaffSystem::GetManagementBonus() const
{
	int32 Sum = 0;
	for (const FStaffMember& M : Roster)
	{
		if (M.bHired && M.Role == EStaffRole::Manager)
		{
			Sum += M.Management;
		}
	}
	return Sum;
}

float UStaffSystem::GetCookingSpeedMultiplier() const
{
	int32 Sum = 0;
	for (const FStaffMember& M : Roster)
	{
		if (M.bHired && M.Role == EStaffRole::Cook)
		{
			Sum += M.Cooking;
		}
	}
	return 1.f + static_cast<float>(Sum) * 0.02f;
}

int32 UStaffSystem::GetCustomerAttractionBonus() const
{
	int32 Sum = 0;
	for (const FStaffMember& M : Roster)
	{
		if (M.bHired)
		{
			Sum += M.Service;
		}
	}
	return FMath::FloorToInt(static_cast<float>(Sum) / 10.f);
}
