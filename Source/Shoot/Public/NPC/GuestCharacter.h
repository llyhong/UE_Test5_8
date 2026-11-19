// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "NPC/GuestTypes.h"
#include "GuestCharacter.generated.h"

class AGuestAIController;
class AChairSeat;
class AGuestManager;
class UOrderHandle;
class UWidgetComponent;
class UTexture2D;
class AShootCharacter;
class AItemActor;
class UNiagaraSystem;
class UNiagaraComponent;

/** 客人订单接单方式 */
UENUM(BlueprintType)
enum class EGuestOrderAcceptMode : uint8
{
	/** 客人坐下后自动点单（无需玩家操作，订单立即产生） */
	AutoAccept UMETA(DisplayName = "自动接单"),
	/** 客人坐下后等待玩家交互（E 键）接单，接单后才产生订单 */
	NeedPlayerAccept UMETA(DisplayName = "需要玩家接单"),
};

/** 客人类型：普通客人 / 特殊客人（特殊客人完成订单给予更高声誉） */
UENUM(BlueprintType)
enum class EGuestType : uint8
{
	Normal  UMETA(DisplayName = "普通客人"),
	Special UMETA(DisplayName = "特殊客人"),
};

/** 状态变化广播：传出状态 + 是否已吃饱（离场时区分“满意离开”与“等不及走了”） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGuestStatusUpdatedSignature,
	EGuestState, State, bool, bSatisfied);

/**
 * 客人 NPC：自动寻路到凳子坐下，等待上菜，吃完后离开并销毁。
 * 由 AGuestAIController 驱动移动。
 */
UCLASS(abstract)
class SHOOT_API AGuestCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AGuestCharacter();

	virtual void Tick(float DeltaSeconds) override;

	/** 目标座位 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest")
	TObjectPtr<AChairSeat> SeatActor;

	/** 生成并管理本客人的管理器（用于销毁前反注册），由 AGuestManager 设置。
	 *  用 UPROPERTY 持有引用，避免被 GC 提前回收。 */
	UPROPERTY()
	TObjectPtr<AGuestManager> Manager;

	/** 由 AGuestManager 在生成时调用，记录管理器引用 */
	void SetManager(AGuestManager* InManager) { Manager = InManager; }

	/** 若本客人仍占用某座位则立即释放（销毁 / 清空时调用，幂等）。供 EndPlay 与 ClearAllGuests 复用 */
	void ReleaseSeatIfOccupied();

	/** 离场目标点（场景边缘） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest")
	FVector LeaveLocation = FVector::ZeroVector;

	/** 进食持续时间（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest")
	float EatDuration = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest|Happy")
	TObjectPtr<UNiagaraSystem> HappyEffectSystem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest|Happy", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float HappyEffectDuration = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest|Happy", meta = (ClampMin = "20.0", ClampMax = "240.0"))
	float HappyEffectWidth = 145.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest|Happy", meta = (ClampMin = "0.0"))
	float HappyEffectTableOffset = 110.0f;

	/** 等待上菜超时（秒），超时直接离开 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest")
	float WaitFoodTimeout = 30.0f;

	/** 排队等待超时（秒）：长时间等不到座位则离场并扣除声誉 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest")
	float QueueWaitTimeout = 20.0f;

	/** 走向座位超时（秒）：超过该时长仍未坐下（被桌子/碰撞挡住、导航不可达），则放弃并离场、释放座位 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest")
	float SeatWalkTimeout = 20.0f;

	/** 特殊客人完成订单时额外提升的声誉（仅在 GuestType==Special 时生效，叠加在 EconomySystem 完成订单的基础声誉之上） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest")
	int32 SpecialReputationBonus = 3;

	/** 是否输出客人的排队/入座/离场等调试信息（屏幕消息与 UE_LOG） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest|Debug")
	bool bShowDebugInfo = true;

	/** 接单方式：自动接单 / 需要玩家接单（默认需要玩家接单；GuestManager 会统一下发覆盖） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest|Order")
	EGuestOrderAcceptMode OrderAcceptMode = EGuestOrderAcceptMode::NeedPlayerAccept;

	/** 客人吃完后是否留下空碟占据座位（玩家按 E 回收才释放）。
	 *  落座时会被 UBaseGameInstance::bUseDirtyPlateFlow（全局总开关）覆盖；
	 *  这里单独设 true/false 可让某类客人强制启用/关闭（用于特殊客人）。默认 true。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest")
	bool bUseDirtyPlateFlow = true;

	/** 坐下后把客人钉在座位上：禁用移动与 RVO 避让、禁止被物理推挤，并在 Tick 中把位置校正回座位。
	 *  避免玩家靠近 / 放下物品 / 其他客人经过时把坐着的客人从椅子上挤开。默认开启。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest|Seat")
	bool bPinToSeatWhenSitting = true;

	/** 钉住时允许的位置偏差（厘米）：超过该值才把客人拉回座位，避免每帧写 Transform 抖动 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Guest|Seat", meta = (ClampMin = "0.0", EditCondition = "bPinToSeatWhenSitting"))
	float SeatPinTolerance = 1.0f;

	/** 坐上 / 离开座位时切换“钉住”状态（坐下时记录座位变换并锁死移动；离座时恢复行走与避让） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest|Seat")
	void SetSeatedPinned(bool bPinned);

	/** 当前是否处于“钉在座位上”的状态 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Guest|Seat")
	bool IsSeatedPinned() const { return bSeatedPinned; }

	/** 当前状态 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Guest")
	EGuestState GuestState = EGuestState::WalkingIn;

	/** 客人类型：普通 / 特殊（特殊客人完成订单获得更高声誉）。由 AGuestManager 按概率在生成时设置 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Guest")
	EGuestType GuestType = EGuestType::Normal;

	/** 当前排队序号（-1 表示未在队列中），由 AGuestManager 维护 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Guest")
	int32 QueueIndex = -1;

	/** 玩家端菜上菜后摆在桌上的那道菜（进食结束后转为“空碟”占据座位） */
	UPROPERTY()
	TObjectPtr<AItemActor> PlateOnTable;

	/** 坐下点餐后产生的订单（未点餐 / 已上菜 / 已超时为 null） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Guest")
	TObjectPtr<UOrderHandle> CurrentOrder = nullptr;

	/** 客人自身计时器（进食/等待上菜超时），不依赖 AI 控制器 */
	FTimerHandle GuestTimerHandle;

	/** 排队等待超时计时器 */
	FTimerHandle QueueTimerHandle;

	/** 离场兜底计时器：走向离场点若导航不可达，超时强制销毁，避免客人永久残留 */
	FTimerHandle LeaveTimeoutTimerHandle;

	/** 头顶状态气泡组件：客人气泡已停用（显隐逻辑移除，组件始终保持隐藏）。
	 *  组件保留是为了兼容派生蓝图里已经存在的组件模板引用。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Guest|Status")
	TObjectPtr<UWidgetComponent> StatusWidgetComp;

	/** 是否已用餐：离场时据此区分“吃饱了”与“等不及走了”的台词 */
	bool bHasEaten = false;

	// ==================== 慌乱 / 摔倒 ====================
	/** 慌乱剩余时间（秒）：>0 时 Tick 做原地抖动，蓝图另有 BP_OnGuestPanicked */
	float PanicRemainingSeconds = 0.f;

	/** 慌乱抖动幅度（度）与已抖时长 */
	float PanicShakeDegrees = 9.f;
	float PanicElapsedSeconds = 0.f;

	/** 第一次慌乱前记录的网格相对旋转：慌乱结束后恢复，避免污染蓝图摆好的姿态 */
	FRotator PanicBaseMeshRotation = FRotator::ZeroRotator;
	bool bPanicBaseMeshCaptured = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Guest|Happy")
	TObjectPtr<UNiagaraComponent> HappyNiagaraComponent;


	/** 摔倒后的免踩期截止时间（世界秒）：防止同一客人每 0.5 秒连续摔 */
	float SlipImmuneUntilWorldTime = 0.f;

	/** 每帧推进慌乱抖动；与坐姿 Tick 开关解耦，走路时慌乱也能表现 */
	void UpdatePanicVisuals(float DeltaSeconds);

	/** 状态变化广播（传出状态 + 是否已吃饱）：保留给已有蓝图监听，客人头顶气泡已停用 */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Guest|Status")
	FGuestStatusUpdatedSignature OnGuestStatusUpdated;

	/** 由 AI 控制器在 Possess 后调用：开始走向座位 */
	void StartWalkingToSeat();

	/** 没有空位时进入排队等待（由 StartWalkingToSeat 或 AI 放弃入座时调用） */
	void JoinQueue();

	/** 在 Center 附近 Radius 范围内徘徊等候（走走停停），由管理器在入队时调用 */
	void StartWaitingWander(const FVector& Center, float Radius, float PauseMin, float PauseMax);

	/** 被管理器提升（有空位了）：重新寻找座位并前往 */
	void PromoteFromQueue();

	/** 在场景中随机寻找一个未被占用的座位（找不到返回 nullptr） */
	AChairSeat* FindRandomFreeSeat() const;

	/** 到达座位后的回调：坐下 */
	UFUNCTION()
	void SitDown();

	/** 外部（玩家上菜逻辑）调用：开始进食 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest")
	void StartEating();

	/** 是否允许玩家端菜上菜（正在等餐且手持菜品时） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest")
	bool CanPlayerServeDish() const;

	/** 玩家端菜走到面前按 E：把菜品摆到桌上并完成该客人的订单（开始进食）。返回是否上菜成功 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest")
	bool TryServeDish(AShootCharacter* Player, AItemActor* Dish);

	/** 进食结束：把桌上菜品转为空碟占据座位，随后离座（方案B） */
	void FinishEating();

	/** 客人点餐：向订单系统请求一个绑定自身的订单，成功返回 true */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest")
	bool PlaceOrder();

	/** 是否允许玩家接单：需要玩家接单模式 + 正在等餐 + 尚未点单 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest|Order")
	bool CanAcceptOrderByPlayer() const;

	/** 玩家按 E 接单：为该客人生成订单，成功返回 true */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest|Order")
	bool AcceptOrderByPlayer();

	/** 订单被完成（上菜成功）时的回调：开始进食（由订单系统调用） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest|Order")
	void OnOrderServed();

	/** 订单超时失败时的回调：离场（由订单系统调用） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest|Order")
	void OnOrderFailed();

	/** 由 AI 控制器在座位寻路彻底失败时调用：让客人直接离场，避免卡在原地 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest")
	void GiveUpAndLeave();

	/** 被突发事件（老鼠等）吓走：走正常离场流程，未完成订单会被取消、座位释放 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest")
	void ScareAway();

	// ==================== 就餐区卫生 / 突发事件表现 ====================
	/** 慌乱一段时间（老鼠出没等）：原地抖动，蓝图用 BP_OnGuestPanicked 播动画 / 音效 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest|Reaction")
	void Panic(float Seconds);

	/** 立即结束慌乱（事件被解决时调用，避免抖动残留） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest|Reaction")
	void StopPanic();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Guest|Reaction")
	bool IsPanicking() const { return PanicRemainingSeconds > 0.f; }

	/** 就餐区脏乱导致满意度下降：直接缩短当前订单的剩余耐心（没有订单时只播表现） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest|Reaction")
	void ApplySatisfactionPenalty(float WaitPenaltySeconds);

	/** 踩到垃圾 / 积水摔倒：播放摔倒表现并短暂慌乱，不强制离场 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest|Reaction")
	void ReactToSlip(float FallSeconds);

	/** 当前是否可能踩到散落物（只在走动 / 排队且不在摔倒冷却期时成立） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Guest|Reaction")
	bool CanSlip() const;

	/** 到达离场点后的回调：销毁 */
	UFUNCTION()
	void OnReachLeavePoint();

protected:
	virtual void BeginPlay() override;

	/** 组件 BeginPlay 会自动 InitWidget 建出气泡控件；这里在它之前清掉蓝图模板配置的控件类，
	 *  确保状态气泡在运行时完全不创建（头顶气泡已停用） */
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 状态变化通知蓝图（UI / 特效） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Guest")
	void OnGuestStateChanged(EGuestState NewState);

	/** 特殊客人被成功接待（上菜完成）时通知蓝图（供金色特效 / 音效 / 飘字）。
	 *  仅在 GuestType == Special 的上菜成功路径触发。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Guest")
	void OnSpecialGuestServed();

	/** 遇到老鼠等突发事件开始慌乱（播惊吓动画 / 音效 / 头顶感叹号） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Guest|Reaction")
	void BP_OnGuestPanicked();

	/** 上菜成功的音效 / 动画入口，头顶庆祝由 Niagara 播放 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Guest|Reaction")
	void BP_OnGuestHappy();

	/** 踩到垃圾 / 积水摔倒（播摔倒动画 / 音效） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Guest|Reaction")
	void BP_OnGuestSlipped();

	/** 就餐区太脏导致满意度下降（头顶冒不满气泡 / 皱眉） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Guest|Reaction")
	void BP_OnSatisfactionDropped(float WaitPenaltySeconds);

	/** 设置状态并广播变化 */
	void SetGuestState(EGuestState NewState);

	/** 动画状态标记：供 ShootAnimInstance 读取以驱动状态机（替代 PlayAnimMontage） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Guest|Anim")
	bool bIsSitting = false;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Guest|Anim")
	bool bIsEating = false;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Guest|Anim")
	bool bIsLeaving = false;

	/** 是否处于“钉在座位上”的状态（坐下后锁死位置，离座前一直有效） */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shoot|Guest|Seat")
	bool bSeatedPinned = false;

	/** 坐下时记录的目标世界变换：座位失效（已转为空碟流程）时仍用它把客人钉在原地 */
	FTransform SeatedTransform = FTransform::Identity;

	/** 排队等待超时：离场并（经管理器）扣除声誉 */
	void OnQueueTimeout();

	/** 等待上菜超时：超时无人上菜则离场（无订单系统时的兜底） */
	void StartWaitFoodTimeout();

	/** 进食结束后开始离开 */
	void StartLeaving();
};
