// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "StaffSystem.generated.h"

/** 员工 / 居民 职业 */
UENUM(BlueprintType)
enum class EStaffRole : uint8
{
	Cook     UMETA(DisplayName = "厨师"),
	Server   UMETA(DisplayName = "服务员"),
	Manager  UMETA(DisplayName = "经理"),
};

/** 可被培养的属性 */
UENUM(BlueprintType)
enum class EStaffAttribute : uint8
{
	Cooking    UMETA(DisplayName = "烹饪"),
	Service    UMETA(DisplayName = "服务"),
	Management UMETA(DisplayName = "管理"),
};

/**
 * 一名员工 / 居民：拥有 烹饪 / 服务 / 管理 三项属性与每月工资。
 * 属性聚合后反哺经营：烹饪→出餐速度，服务→服务质量(招客/声誉)，管理→降低维护。
 */
USTRUCT(BlueprintType)
struct FStaffMember
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
	EStaffRole Role = EStaffRole::Cook;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
	int32 Cooking = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
	int32 Service = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
	int32 Management = 1;

	/** 每月工资（结算时从营收中扣除） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
	int32 Wage = 100;

	/** 是否已入职（true=在编，参与属性聚合与发薪） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
	bool bHired = false;
};

/**
 * 员工 / 居民 管理系统（开罗式经营）：招募、培养属性，并对外提供聚合加成。
 * 不持有金钱，发薪 / 培养花费由 UBusinessSimulation 通过经济系统扣减。
 */
UCLASS(Blueprintable)
class SHOOT_API UStaffSystem : public UObject
{
	GENERATED_BODY()

public:
	/** 在编 + 待入职候选名册（HireStaff 后 bHired=true 才计入加成与发薪） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
	TArray<FStaffMember> Roster;

	/** 招募一名员工；HireCost 不足则返回 -1，否则返回在 Roster 中的下标 */
	UFUNCTION(BlueprintCallable, Category = "Staff")
	int32 HireStaff(const FStaffMember& Candidate, int32 HireCost);

	/** 培养某员工某项属性 +1；TrainingCost 由调用方先扣款，返回是否成功（下标越界则失败） */
	UFUNCTION(BlueprintCallable, Category = "Staff")
	bool TrainStaff(int32 Index, EStaffAttribute Attr, int32 TrainingCost);

	/** 解雇（移出名册，停止发薪与加成） */
	UFUNCTION(BlueprintCallable, Category = "Staff")
	void FireStaff(int32 Index);

	/** 在编员工每月工资总额 */
	UFUNCTION(BlueprintCallable, Category = "Staff")
	int32 GetTotalMonthlyWages() const;

	/** 服务质量（0~N）：在编员工服务属性之和，用于提升声誉增速与招客 */
	UFUNCTION(BlueprintCallable, Category = "Staff")
	float GetServiceQuality() const;

	/** 管理加成（整数点）：在编经理管理属性之和，用于降低每日维护支出 */
	UFUNCTION(BlueprintCallable, Category = "Staff")
	int32 GetManagementBonus() const;

	/** 出餐速度倍率：1 + 烹饪属性之和 * 0.02（每点 +2%） */
	UFUNCTION(BlueprintCallable, Category = "Staff")
	float GetCookingSpeedMultiplier() const;

	/** 招客加成（整数）：服务属性之和 / 10，作为额外可容纳客人数 */
	UFUNCTION(BlueprintCallable, Category = "Staff")
	int32 GetCustomerAttractionBonus() const;
};
