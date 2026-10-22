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
class USphereComponent;
class UWidgetComponent;
class UGuestStatusWidget;
class UTexture2D;

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

/** 头顶状态变化广播：传出状态 + 是否已吃饱（离场时区分“满意离开”与“等不及走了”），
 *  固定台词 / 颜色 / 贴图由监听的 GuestStatusWidget 从配置表按状态随机抽取。 */
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

	/** 目标座位 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest")
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest")
	FVector LeaveLocation = FVector::ZeroVector;

	/** 进食持续时间（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest")
	float EatDuration = 5.0f;

	/** 等待上菜超时（秒），超时直接离开 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest")
	float WaitFoodTimeout = 30.0f;

	/** 排队等待超时（秒）：长时间等不到座位则离场并扣除声誉 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest")
	float QueueWaitTimeout = 20.0f;

	/** 走向座位超时（秒）：超过该时长仍未坐下（被桌子/碰撞挡住、导航不可达），则放弃并离场、释放座位 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest")
	float SeatWalkTimeout = 20.0f;

	/** 特殊客人完成订单时额外提升的声誉（仅在 GuestType==Special 时生效，叠加在 EconomySystem 完成订单的基础声誉之上） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest")
	int32 SpecialReputationBonus = 3;

	/** 是否输出客人的排队/入座/离场等调试信息（屏幕消息与 UE_LOG） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Debug")
	bool bShowDebugInfo = true;

	/** 接单方式：自动接单 / 需要玩家接单（默认自动，保持原有行为；GuestManager 会统一下发） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Order")
	EGuestOrderAcceptMode OrderAcceptMode = EGuestOrderAcceptMode::AutoAccept;

	/** 当前状态 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guest")
	EGuestState GuestState = EGuestState::WalkingIn;

	/** 客人类型：普通 / 特殊（特殊客人完成订单获得更高声誉）。由 AGuestManager 按概率在生成时设置 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guest")
	EGuestType GuestType = EGuestType::Normal;

	/** 当前排队序号（-1 表示未在队列中），由 AGuestManager 维护 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guest")
	int32 QueueIndex = -1;

	/** 坐下点餐后产生的订单（未点餐 / 已上菜 / 已超时为 null） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guest")
	TObjectPtr<UOrderHandle> CurrentOrder = nullptr;

	/** 交互范围：玩家靠近后可按 E 接单（仅"需要玩家接单"模式使用） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guest|Components")
	TObjectPtr<USphereComponent> InteractionSphere;

	/** 客人自身计时器（进食/等待上菜超时），不依赖 AI 控制器 */
	FTimerHandle GuestTimerHandle;

	/** 排队等待超时计时器 */
	FTimerHandle QueueTimerHandle;

	/** 离场兜底计时器：走向离场点若导航不可达，超时强制销毁，避免客人永久残留 */
	FTimerHandle LeaveTimeoutTimerHandle;

	/** 头顶状态气泡组件（屏幕空间，始终面向相机） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guest|Status")
	TObjectPtr<UWidgetComponent> StatusWidgetComp;

	/** 是否已用餐：离场时据此区分“吃饱了”与“等不及走了”的台词 */
	bool bHasEaten = false;

	/** 状态变化时广播 OnGuestStatusUpdated（传出状态 + 是否已吃饱），由 GuestStatusWidget 从配置表随机抽取显示 */
	void UpdateStatusWidget(EGuestState NewState);

	/** 头顶状态变化广播（GuestStatusWidget 蓝图在 Construct 时绑定监听，从配置表按状态抽台词/颜色/贴图显示） */
	UPROPERTY(BlueprintAssignable, Category = "Guest|Status")
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
	UFUNCTION(BlueprintCallable, Category = "Guest")
	void StartEating();

	/** 客人点餐：向订单系统请求一个绑定自身的订单，成功返回 true */
	UFUNCTION(BlueprintCallable, Category = "Guest")
	bool PlaceOrder();

	/** 是否允许玩家接单：需要玩家接单模式 + 正在等餐 + 尚未点单 */
	UFUNCTION(BlueprintCallable, Category = "Guest|Order")
	bool CanAcceptOrderByPlayer() const;

	/** 玩家按 E 接单：为该客人生成订单，成功返回 true */
	UFUNCTION(BlueprintCallable, Category = "Guest|Order")
	bool AcceptOrderByPlayer();

	/** 订单被完成（上菜成功）时的回调：开始进食（由订单系统调用） */
	UFUNCTION(BlueprintCallable, Category = "Guest|Order")
	void OnOrderServed();

	/** 订单超时失败时的回调：离场（由订单系统调用） */
	UFUNCTION(BlueprintCallable, Category = "Guest|Order")
	void OnOrderFailed();

	/** 由 AI 控制器在座位寻路彻底失败时调用：让客人直接离场，避免卡在原地 */
	UFUNCTION(BlueprintCallable, Category = "Guest")
	void GiveUpAndLeave();

	/** 到达离场点后的回调：销毁 */
	UFUNCTION()
	void OnReachLeavePoint();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 状态变化通知蓝图（UI / 特效） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Guest")
	void OnGuestStateChanged(EGuestState NewState);

	/** 特殊客人被成功接待（上菜完成）时通知蓝图（供金色特效 / 音效 / 飘字）。
	 *  仅在 GuestType == Special 的上菜成功路径触发。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Guest")
	void OnSpecialGuestServed();

	/** 设置状态并广播变化 */
	void SetGuestState(EGuestState NewState);

	/** 动画状态标记：供 ShootAnimInstance 读取以驱动状态机（替代 PlayAnimMontage） */
	UPROPERTY(BlueprintReadOnly, Category = "Guest|Anim")
	bool bIsSitting = false;

	UPROPERTY(BlueprintReadOnly, Category = "Guest|Anim")
	bool bIsEating = false;

	UPROPERTY(BlueprintReadOnly, Category = "Guest|Anim")
	bool bIsLeaving = false;

	/** 排队等待超时：离场并（经管理器）扣除声誉 */
	void OnQueueTimeout();

	/** 等待上菜超时：超时无人上菜则离场（无订单系统时的兜底） */
	void StartWaitFoodTimeout();

	/** 交互球：玩家进入范围时注册为可接单目标（仅"需要玩家接单"模式） */
	UFUNCTION()
	void OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** 交互球：玩家离开范围时反注册 */
	UFUNCTION()
	void OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** 进食结束后开始离开 */
	void StartLeaving();
};
