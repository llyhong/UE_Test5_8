// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPC/GuestCharacter.h"
#include "GuestManager.generated.h"

class AGuestCharacter;

/**
 * 客人统一管理器：负责按配置周期性生成 / 销毁 GuestCharacter，
 * 维护在场客人列表，并约束最大同时在场数量（避免抢占过多座位）。
 * 在关卡中放置一个此 Actor，并指定 GuestClass 为你的客人蓝图。
 */
UCLASS()
class SHOOT_API AGuestManager : public AActor
{
	GENERATED_BODY()

public:
	AGuestManager();

	/** 要生成的客人蓝图类（需继承自 AGuestCharacter） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager")
	TSubclassOf<AGuestCharacter> GuestClass;

	/** 客人生成点；若为空则使用管理器自身位置 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager")
	FVector SpawnLocation = FVector::ZeroVector;

	/** 客人离场目标点（传给每个生成出的客人） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager")
	FVector LeaveLocation = FVector::ZeroVector;

	/** 最大同时在场客人数量（基础值；实际容量还会叠加声誉与“新餐桌”道具加成） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager")
	int32 MaxGuests = 6;

	/** 容量随声誉提升：每多少声誉 +1 容量（数值越小，声誉对客人数量的拉动越明显） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager")
	int32 GuestsPerReputation = 10;

	/** 每持有 1 个“新餐桌”(道具ID=1001) 额外增加的容量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager")
	int32 ExtraGuestsPerTable = 2;

	/** 容量绝对上限（防止数值爆炸） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager")
	int32 MaxGuestsHardCap = 30;

	/** 自动生成间隔（秒），<=0 表示不自动生成 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager")
	float SpawnInterval = 4.0f;

	/** 是否自动开始生成 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager")
	bool bAutoSpawn = true;

	/** 是否打印生成诊断信息（失败原因、生成坐标、场景座位数等） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager")
	bool bShowDebugInfo = true;

	/** 接单方式：自动接单 / 需要玩家接单（会统一覆盖下发给每个生成出的客人） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager|Order")
	EGuestOrderAcceptMode OrderAcceptMode = EGuestOrderAcceptMode::AutoAccept;

	/** 生成特殊客人的概率（0~1）。特殊客人完成订单会获得额外声誉（见 AGuestCharacter::SpecialReputationBonus） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpecialGuestChance = 0.2f;

	/** 当前在场客人列表 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GuestManager")
	TArray<TObjectPtr<AGuestCharacter>> ActiveGuests;

	/** 正在排队等待的客人（找不到座位时在此等候，队首最先入座） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GuestManager")
	TArray<TObjectPtr<AGuestCharacter>> WaitingQueue;

	/** 排队等候时客人的徘徊范围半径（cm）：客人会在管理器附近该范围内随机游走 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager|Queue", meta = (ClampMin = "50.0"))
	float QueueWanderRadius = 250.0f;

	/** 徘徊时每次驻足（停）的最短时长（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager|Queue", meta = (ClampMin = "0.0"))
	float QueueWanderPauseMin = 0.5f;

	/** 徘徊时每次驻足（停）的最长时长（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager|Queue", meta = (ClampMin = "0.0"))
	float QueueWanderPauseMax = 2.0f;

	/** 排队等待超时后扣除的声誉 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager|Queue")
	int32 ReputationPenaltyOnWaitTimeout = 3;

	/** 是否绘制等候区可视化（蓝色圆环=徘徊范围，绿球=每位等待客人当前位置） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager|Debug")
	bool bDrawQueueDebug = true;

	/** 是否绘制生成点/离场点（绿=Spawn，红=Leave，黄=Leave 为默认零向量警告），方便检查配置 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager|Debug")
	bool bDrawSpawnLeaveDebug = true;

	/** 座位占用情况自动打印间隔（秒）；<=0 表示不自动打印，可随时调用 PrintSeatStatus 手动打印 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager|Debug", meta = (ClampMin = "0.0"))
	float SeatStatusLogInterval = 0.f;

	/** 调试球半径（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager|Debug", meta = (ClampMin = "1.0"))
	float QueueDebugSphereRadius = 30.0f;

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;

	/** 立即生成一个客人（受 MaxGuests 限制）。返回生成的实例，或 nullptr */
	UFUNCTION(BlueprintCallable, Category = "GuestManager")
	AGuestCharacter* SpawnGuest();

	/** 一次性连续生成最多 Count 个客人（仍受 MaxGuests 限制，满了即停）。供招揽台调用 */
	UFUNCTION(BlueprintCallable, Category = "GuestManager")
	void SpawnGuestsBurst(int32 Count);

	/** 实际可容纳上限 = 基础 + 声誉加成 + 新餐桌道具加成（运行时动态计算） */
	UFUNCTION(BlueprintCallable, Category = "GuestManager")
	int32 GetEffectiveMaxGuests() const;

	/** 由客人销毁前调用，把自己从在场列表移除 */
	void UnregisterGuest(AGuestCharacter* Guest);

	/** 清空并销毁所有在场客人 */
	UFUNCTION(BlueprintCallable, Category = "GuestManager")
	void ClearAllGuests();

	/** 暂停自动生成（打烊/过渡期间调用，避免清场后倒计时内又冒出新客人重新占座） */
	UFUNCTION(BlueprintCallable, Category = "GuestManager")
	void PauseSpawning();

	/** 恢复自动生成（进入下一关后调用） */
	UFUNCTION(BlueprintCallable, Category = "GuestManager")
	void ResumeSpawning();

	/** 客人入队：返回分配的排队序号，并把它摆到对应的队列位置（重复入队会移到队尾） */
	UFUNCTION(BlueprintCallable, Category = "GuestManager|Queue")
	int32 EnqueueGuest(AGuestCharacter* Guest);

	/** 客人离队（入座/超时/离场时调用），并重新排布剩余客人 */
	UFUNCTION(BlueprintCallable, Category = "GuestManager|Queue")
	void RemoveFromQueue(AGuestCharacter* Guest);

	/** 等候徘徊区的中心点（即管理器位置），供徘徊与调试绘制使用 */
	UFUNCTION(BlueprintCallable, Category = "GuestManager|Queue")
	FVector GetQueueWanderCenter() const;

	/** 有空位时调用：把队首等待客人提升去占座 */
	UFUNCTION(BlueprintCallable, Category = "GuestManager|Queue")
	void OnSeatFreed();

	/** 打印场景上所有座位的占用情况（占用/空闲及占用者），用于调试；蓝图/控制台可随时调用 */
	UFUNCTION(BlueprintCallable, Category = "GuestManager|Debug")
	void PrintSeatStatus();

	/** 排队等待超时：移出队列并扣除声誉（由客人调用） */
	void HandleQueueTimeout(AGuestCharacter* Guest);

private:
	/** 自动生成用的定时器 */
	FTimerHandle AutoSpawnTimerHandle;

	/** 座位占用情况自动打印用的定时器 */
	FTimerHandle SeatStatusLogHandle;

	/** 定时器回调（SetTimer 要求返回 void），内部调用 SpawnGuest */
	void TickSpawn();

	/** 客人变动后刷新剩余等待客人的排队序号（不再摆放位置，位置由徘徊决定） */
	void RefreshQueueSlots();

	/** 每帧绘制等候徘徊范围与各等待客人位置（仅在 bDrawQueueDebug 时生效） */
	void DrawQueueDebug();

	/** 绘制生成点/离场点（仅在 bDrawSpawnLeaveDebug 时生效） */
	void DrawSpawnLeaveDebug() const;
};
