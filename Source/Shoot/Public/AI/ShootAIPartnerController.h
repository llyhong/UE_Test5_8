// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ShootAIPartnerController.generated.h"

class AShootCharacter;

/** AI 搭档（单人测试用"第二个玩家"）行为模式：
 *  Follow  = 跟随玩家并捡起脚边掉落物（最小占位，主要用来凑双人压力测平衡）；
 *  Forager = 平时守店，备料窗口一开自动去野外采集，窗口结束回店（外勤位）；
 *  Helper  = 在店里自动取出锅里的成品菜、端给等餐客人（帮厨位）。 */
UENUM(BlueprintType)
enum class EAIPartnerMode : uint8
{
	Follow UMETA(DisplayName = "跟随"),
	Forager UMETA(DisplayName = "外勤采集"),
	Helper UMETA(DisplayName = "帮厨上菜"),
};

/**
 * 单人测试用的 AI 搭档控制器：操控一个 ShootCharacter pawn，
 * 不吃玩家输入，靠"走位 + 直接调用角色交互函数（Pick/HarvestForCharacter）"干活。
 * 由 UBaseGameInstance::SpawnAIPartner 生成；运行时 SetPartnerMode 切换职责。
 */
UCLASS()
class SHOOT_API AShootAIPartnerController : public AAIController
{
	GENERATED_BODY()

public:
	/** 当前行为模式（运行时可在 跟随/外勤/帮厨 之间切换） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|AIPartner")
	EAIPartnerMode Mode = EAIPartnerMode::Forager;

	/** 跟随目标（玩家 pawn）。为空时自动选第一个非自己的本地玩家 pawn。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|AIPartner")
	TObjectPtr<APawn> FollowTarget;

	/** 平时驻守点（店内待命位置）：外勤/帮厨没事干时站这里；零向量表示原地待命 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|AIPartner")
	FVector IdlePostLocation = FVector::ZeroVector;

	/** 跟随模式的保持距离（cm），超过就追，小于就停 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|AIPartner", meta = (ClampMin = "50.0"))
	float FollowDistance = 250.f;

	/** 进入目标的此距离（cm）内才执行交互/采集 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|AIPartner", meta = (ClampMin = "20.0"))
	float InteractRange = 150.f;

	/** 跟随模式的拾取尝试间隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|AIPartner", meta = (ClampMin = "0.2"))
	float PickTryInterval = 0.8f;

	/** 空闲（没菜可取、没客人要送、非备料窗口）时在驻守点附近小范围溜达，避免长时间杵在原地像根柱子 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|AIPartner|Idle")
	bool bIdleWander = true;

	/** 溜达半径（cm）：在驻守点周围随机取可达点，太大容易走到玩家操作区里碍事 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|AIPartner|Idle", meta = (ClampMin = "0.0"))
	float IdleWanderRadius = 220.f;

	/** 到点后的随机停顿区间（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|AIPartner|Idle", meta = (ClampMin = "0.0"))
	float IdleWanderPauseMin = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|AIPartner|Idle", meta = (ClampMin = "0.0"))
	float IdleWanderPauseMax = 4.5f;

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Shoot|AIPartner")
	void SetPartnerMode(EAIPartnerMode NewMode);

	EAIPartnerMode GetPartnerMode() const { return Mode; }

private:
	AShootCharacter* GetPartnerChar() const;
	APawn* ResolveFollowTarget() const;
	class AGuestManager* FindGuestManager() const;

	/** 距离大于 Range 才发寻路；返回 true 表示"已在交互范围内" */
	bool MoveWithinRange(const FVector& TargetLocation, float Range);
	void DoPick();
	void TickFollow(float DeltaSeconds);
	void TickForager();
	void TickHelper();

	/** 在 Center 附近小范围溜达（到达后随机停几秒再换点）；Center 为零向量或未开启时退回原地待命 */
	void TickIdleWander(const FVector& Center);

	float PickCooldown = 0.f;
	FVector PendingDestination = FVector::ZeroVector;
	bool bHasPendingDestination = false;
	/** 空闲溜达：当前目标点、是否已有目标、到点后的剩余停顿时间 */
	FVector IdleWanderTarget = FVector::ZeroVector;
	bool bHasIdleWanderTarget = false;
	float IdleWanderPauseLeft = 0.f;
	/** 上一次发寻路以来的冷却：寻路中断（路径尽头/目标出导航网）后据此节流重试，避免每帧重发 */
	float MoveRetryTimer = 0.f;
	/** 收割失败计数：同株连续失败 N 次就暂时拉黑，防止卡在不可达的采集点上 */
	int32 HarvestFailCount = 0;
	TWeakObjectPtr<class APlant> LastHarvestTarget;
	float HarvestBlacklistUntil = 0.f;
};
