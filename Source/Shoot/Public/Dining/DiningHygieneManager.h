// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Event/RandomEventManager.h"
#include "Dining/DiningMessActor.h"
#include "DiningHygieneManager.generated.h"

class UTimeSystem;

/**
 * 就餐区卫生管理器（关卡里放一个）。
 *
 * 现实餐厅运营的可回应部分：
 *  - 营业期间每隔一段时间在就餐区随机撒垃圾 / 积水（白模），数量有上限；
 *  - 玩家靠近按 E 清理（ADiningMessActor 自己注册交互，本管理器只统计）；
 *  - 散落物放太久：按周期扣声誉，并让在场客人的订单耐心变短（满意度下降）；
 *  - 客人在就餐区走动时可能踩到垃圾摔倒：立即赔偿金额（钱不够记未付账单）并再扣一点声誉。
 *
 * 所有数值都在本 Actor 上可调；生成区域请在 SpawnAreas 里标好就餐区（同一套 FEventSpawnArea）。
 */
UCLASS(Blueprintable)
class SHOOT_API ADiningHygieneManager : public AActor
{
	GENERATED_BODY()

public:
	ADiningHygieneManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ==================== 生成配置 ====================
	/** 撒点区域：只取这里的区域；为空时以本 Actor 位置为圆心、600cm 半径 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Spawn")
	TArray<FEventSpawnArea> SpawnAreas;

	/** 散落物类；留空使用本模块的 ADiningMessActor（白模） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Spawn")
	TSubclassOf<ADiningMessActor> MessClass;

	/** 是否自动随机产生散落物 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Spawn")
	bool bAutoSpawn = true;

	/** 两次生成之间的间隔（秒，区间随机） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Spawn", meta = (ClampMin = "1.0"))
	float SpawnIntervalMin = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Spawn", meta = (ClampMin = "1.0"))
	float SpawnIntervalMax = 45.f;

	/** 同时存在的散落物上限：达到后不再生成 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Spawn", meta = (ClampMin = "0"))
	int32 MaxActiveMesses = 6;

	/** 生成时是垃圾的概率（其余为积水） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Spawn", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TrashChance = 0.6f;

	/** 只在营业期间生成（打烊后不再产生新的散落物，但已有的仍可清理） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Spawn")
	bool bOnlyWhenBusinessOpen = true;

	/** 开局先撒几个，保证一进门就能看到（便于测试与传达玩法） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Spawn")
	bool bSpawnInitialMesses = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Spawn", meta = (EditCondition = "bSpawnInitialMesses", ClampMin = "0"))
	int32 InitialMessCount = 3;

	// ==================== 未清理惩罚 ====================
	/** 生成后多少秒内清理不扣分（宽限） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Penalty", meta = (ClampMin = "0.0"))
	float CleanGraceSeconds = 45.f;

	/** 超时后每隔多少秒结算一次惩罚（每个未清理的散落物各自计时） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Penalty", meta = (ClampMin = "1.0"))
	float PenaltyIntervalSeconds = 10.f;

	/** 每次惩罚扣的声誉（正数，扣减量） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Penalty", meta = (ClampMin = "0"))
	int32 ReputationLossPerPenalty = 1;

	/** 每次惩罚让在场客人订单耐心缩短的秒数（模拟满意度下降） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Penalty", meta = (ClampMin = "0.0"))
	float GuestWaitTimePenaltySeconds = 3.f;

	// ==================== 顾客摔倒 ====================
	/** 是否启用“客人踩到散落物摔倒” */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Slip")
	bool bEnableSlip = true;

	/** 摔倒判定频率（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Slip", meta = (ClampMin = "0.1"))
	float SlipCheckInterval = 0.5f;

	/** 判定半径（cm）：客人离散落物多近算踩到 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Slip", meta = (ClampMin = "0.0"))
	float SlipRadius = 90.f;

	/** 每次判定每位客人摔倒的概率（0.05 = 5%） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Slip", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SlipChancePerCheck = 0.05f;

	/** 摔倒后慌乱 / 爬起的持续时间（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Slip", meta = (ClampMin = "0.5"))
	float SlipFallSeconds = 2.f;

	/** 摔倒赔偿金额（从餐厅扣钱；钱不够时记入未付账单） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Slip", meta = (ClampMin = "0"))
	int32 SlipCompensationMoney = 20;

	/** 摔倒额外扣的声誉 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Slip", meta = (ClampMin = "0"))
	int32 SlipReputationLoss = 2;

	// ==================== 运行时 / 调试 ====================
	/** 当前场上散落物 */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|DiningHygiene|Runtime")
	TArray<TObjectPtr<ADiningMessActor>> ActiveMesses;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningHygiene|Debug")
	bool bShowDebugInfo = true;

	/** 立即在指定位置生成一个散落物（调试 / 蓝图用） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|DiningHygiene")
	ADiningMessActor* SpawnMess(EDiningMessType Type, FVector Location);

	/** 在就餐区随机撒一个（默认垃圾 / 积水按 TrashChance）；成功返回生成的 Actor */
	UFUNCTION(BlueprintCallable, Category = "Shoot|DiningHygiene")
	ADiningMessActor* SpawnRandomMess();

	UFUNCTION(BlueprintPure, Category = "Shoot|DiningHygiene")
	int32 GetActiveMessCount() const { return ActiveMesses.Num(); }

protected:
	/** 已绑定的时间系统（营业状态判定；关卡重建时钟时幂等重绑） */
	UPROPERTY()
	TObjectPtr<UTimeSystem> BoundTimeSystem;

	FRandomStream Rand;
	bool bRandReady = false;

	/** 下次自动生成的绝对时间 */
	float NextSpawnWorldTime = 0.f;

	/** 距离下次摔倒判定的剩余时间 */
	float SlipCheckCountdown = 0.f;

	/** 每个散落物超出宽限后的惩罚累计时间 */
	TMap<TWeakObjectPtr<ADiningMessActor>, float> PenaltyAccumSeconds;

	void EnsureRandomReady();
	void EnsureTimeSystemBound();

	/** 当前是否允许自动生成（营业时段判定） */
	bool IsSpawningAllowed() const;

	/** 随机取一个生成区域；找不到返回 nullptr */
	const FEventSpawnArea* PickSpawnArea() const;

	/** 在区域内随机取一个贴地点；与已有散落物太近时重试 */
	FVector PickSpawnLocation() const;

	/** 未清理惩罚：扣声誉 + 压低在场客人耐心 */
	void ApplyPenalties(float DeltaSeconds);

	/** 顾客摔倒判定 */
	void CheckGuestSlips(float DeltaSeconds);

	/** 支付摔倒赔偿：钱不够记未付账单 */
	void PaySlipCompensation();

	/** 清理已被销毁的散落物记录（玩家清理 / 关卡卸载） */
	void PruneMesses();

	UFUNCTION()
	void HandleMessCleaned(ADiningMessActor* Mess);

	void ShowDebug(const FString& Message, const FLinearColor& Color) const;
};
