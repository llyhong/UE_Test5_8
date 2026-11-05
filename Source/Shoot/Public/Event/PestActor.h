// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PestActor.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class AShootCharacter;

/** 玩家抓住害虫时广播（Manager 用它统计进度 / 判定事件成功） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPestCaught, APestActor*, Pest);

/**
 * 可捕捉的"害虫"（老鼠等突发事件用）。
 *
 * 行为：在出生点附近随机游走 → 玩家靠近 FleeRadius 内转身逃跑 →
 *      开启 bScareGuests 时靠近客人会把客人吓走（客人走正常离场流程，未完成订单被取消）。
 * 抓取：交互球内玩家按 E（由 ShootCharacter 转发进来）→ 广播 OnCaught 并销毁。
 *
 * 外观完全交给蓝图子类（如 BP_Mouse 配静态网格 / 材质 / 音效）；
 * 代码只负责移动、逃跑、吓客人与抓取判定，所有参数均可在蓝图实例或事件配置表里覆盖。
 */
UCLASS(Blueprintable)
class SHOOT_API APestActor : public AActor
{
	GENERATED_BODY()

public:
	APestActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ==================== 移动 ====================
	/** 正常游走速度（cm/s） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Move", meta = (ClampMin = "0"))
	float MoveSpeed = 150.f;

	/** 被玩家靠近时的逃跑速度（cm/s） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Move", meta = (ClampMin = "0"))
	float FleeSpeed = 260.f;

	/** 玩家进入该距离后开始逃跑（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Move", meta = (ClampMin = "0"))
	float FleeRadius = 350.f;

	/** 围绕出生点的活动半径（cm）：不会跑出这个范围 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Move", meta = (ClampMin = "0"))
	float WanderRadius = 450.f;

	/** 每次重新选目标点的间隔（秒，区间随机） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Move", meta = (ClampMin = "0.1"))
	float RepathIntervalMin = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Move", meta = (ClampMin = "0.1"))
	float RepathIntervalMax = 1.6f;

	// ==================== 贴地 ====================
	/** 移动后向下打线贴地，避免悬空 / 穿到地板下 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Ground")
	bool bSnapToGround = true;

	/** 打线起点：当前位置上方多少 cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Ground", meta = (EditCondition = "bSnapToGround", ClampMin = "0"))
	float TraceUp = 200.f;

	/** 打线长度：向下多少 cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Ground", meta = (EditCondition = "bSnapToGround", ClampMin = "0"))
	float TraceDown = 500.f;

	/** 贴地后的抬高量（cm）：网格原点在底部时用 0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Ground", meta = (EditCondition = "bSnapToGround"))
	float GroundOffset = 0.f;

	// ==================== 吓跑客人 ====================
	/** 靠近客人时是否把客人吓走（客人离场会取消未完成订单） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Scare")
	bool bScareGuests = true;

	/** 吓跑客人的触发距离（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Scare", meta = (EditCondition = "bScareGuests", ClampMin = "0"))
	float ScareRadius = 250.f;

	/** 检查客人的间隔（秒），避免每帧全场景遍历 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Scare", meta = (EditCondition = "bScareGuests", ClampMin = "0.1"))
	float ScareInterval = 0.75f;

	// ==================== 交互 ====================
	/** 玩家可抓取距离（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Interaction", meta = (ClampMin = "0"))
	float InteractionRadius = 140.f;

	/** 打印调试信息（事件开始 / 抓取 / 吓客） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Pest|Debug")
	bool bShowDebugInfo = false;

	// ==================== 事件 ====================
	/** 被玩家抓住时广播（销毁前触发） */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Pest|Events")
	FOnPestCaught OnCaught;

	/** 被抓时的蓝图表现（音效 / 粒子 / 飘字），在 Destroy 前调用 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Pest")
	void BP_OnCaught();

	/** 某位客人被本害虫吓走时通知蓝图（可做音效 / 表情） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Pest")
	void BP_OnScaredGuest(APawn* Guest);

	// ==================== 接口 ====================
	/** 玩家按 E 抓取：成功返回 true（已抓过 / 玩家无效返回 false） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Pest")
	bool TryCatch(AShootCharacter* Player);

	/** 由随机事件管理器按配置表参数覆盖默认值（生成后立刻调用） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Pest")
	void Configure(float InMoveSpeed, float InFleeRadius, float InWanderRadius, bool bInScareGuests, float InScareRadius);

protected:
	/** 外观网格（蓝图里换成老鼠模型；默认不阻挡） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Pest|Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

	/** 玩家交互球（只做 Overlap 查询，不阻挡） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Pest|Components")
	TObjectPtr<USphereComponent> InteractionSphere;

	UFUNCTION()
	void OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** 选一个新的游走目标点（限制在出生点 WanderRadius 内） */
	void PickNewWanderTarget();

	/** 把位置贴到地面（打不到地面则保持原 Z） */
	void SnapToGround(FVector& InOutLocation) const;

	/** 定期检查附近客人并把他们吓走 */
	void UpdateGuestScaring(float DeltaSeconds);

	/** 当前玩家 Pawn（缓存） */
	UPROPERTY()
	TObjectPtr<APawn> CachedPlayer;

private:
	/** 出生点（活动范围圆心） */
	FVector HomeLocation = FVector::ZeroVector;

	/** 当前游走目标点 */
	FVector MoveTarget = FVector::ZeroVector;

	/** 距离下次重新选点的剩余时间 */
	float RepathCountdown = 0.f;

	/** 距离下次检查客人的剩余时间 */
	float ScareCountdown = 0.f;

	/** 是否已被抓住（防止重复结算） */
	bool bCaught = false;

	FRandomStream Rand;
};
