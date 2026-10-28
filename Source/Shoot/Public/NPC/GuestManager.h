// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPC/GuestCharacter.h"
#include "GuestManager.generated.h"

class AGuestCharacter;

/** 打烊收尾完成广播：已点菜的客人都处理完（上完菜 / 等待超时离场）后才触发，表示"真正打烊、可以结束这一天" */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnClosingFinishedSignature);

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

	/** 接单方式：自动接单 / 需要玩家接单（会统一覆盖下发给每个生成出的客人）。
	 *  默认“需要玩家接单”：客人坐下后等待玩家靠近按 E 接单，接单后才真正出现订单。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager|Order")
	EGuestOrderAcceptMode OrderAcceptMode = EGuestOrderAcceptMode::NeedPlayerAccept;

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

	// ========== 打烊收尾（延迟真正打烊） ==========
	/** 打烊时是否保留"已点菜"的客人：等玩家把菜上完，或客人等待超时离场，才真正打烊结束这一天。
	 *  关闭则退回旧行为（打烊立刻清走所有客人）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager|Closing")
	bool bKeepOrderedGuestsOnClose = true;

	/** 打烊收尾的最长等待时间（秒）：兜底防止客卡住导致永远打不了烊。<=0 表示不设上限。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestManager|Closing", meta = (ClampMin = "0.0"))
	float ClosingMaxWaitTime = 120.f;

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

	// —— 打烊收尾（延迟真正打烊） ——
	/** 是否处于"打烊收尾中"：已打烊但还有已点菜客人未处理完 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "GuestManager|Closing")
	bool IsClosingPending() const { return bClosingPending; }

	/** 该客人是否需要在打烊时保留（已点菜 / 正在进食：等上菜或超时后才算处理完） */
	UFUNCTION(BlueprintCallable, Category = "GuestManager|Closing")
	bool IsGuestNeedKeepOnClose(const AGuestCharacter* Guest) const;

	/** 统计当前仍需保留（未处理完）的客人数 */
	UFUNCTION(BlueprintCallable, Category = "GuestManager|Closing")
	int32 GetPendingClosingGuestCount() const;

	/** 真正完成打烊：清掉剩余客人并广播 OnClosingFinished（由收尾检测/兜底超时调用） */
	UFUNCTION(BlueprintCallable, Category = "GuestManager|Closing")
	void FinishClosing();

	/** 打印场景上所有座位的占用情况（占用/空闲及占用者），用于调试；蓝图/控制台可随时调用 */
	UFUNCTION(BlueprintCallable, Category = "GuestManager|Debug")
	void PrintSeatStatus();

	/** 打烊收尾完成：已点菜的客人都处理完（上完菜 / 等待超时离场），可以真正结束这一天。
	 *  蓝图/关卡可监听此事件做结算、切下一天等。 */
	UPROPERTY(BlueprintAssignable, Category = "GuestManager|Closing")
	FOnClosingFinishedSignature OnClosingFinished;

	/** 排队等待超时：移出队列并扣除声誉（由客人调用） */
	void HandleQueueTimeout(AGuestCharacter* Guest);

private:
	/** 营业状态变化：开营恢复自动生成，打烊停生成并清场 */
	UFUNCTION()
	void OnBusinessOpenChanged(bool bOpen);

	/** 是否已绑定 TimeSystem 营业委托（懒绑定，避免 BeginPlay 顺序问题） */
	bool bBoundBusiness = false;

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

	/** 打烊时清走"不需要保留"的客人（保留已点菜的），用于延迟打烊 */
	void ClearGuestsExcept(const TArray<AGuestCharacter*>& KeepList);

	/** 收尾检测：若已无待处理客人则真正完成打烊 */
	void CheckClosingFinished();

	/** 是否处于打烊收尾中（已打烊但仍有已点菜客人未处理完） */
	bool bClosingPending = false;

	/** 打烊收尾兜底超时定时器 */
	FTimerHandle ClosingTimeoutHandle;
};
