// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "GuestAIController.generated.h"

class AGuestCharacter;

/**
 * 客人 AI 控制器：驱动 NPC 寻路到座位、坐下、离开。
 * 使用 AAIController 内置的 MoveTo 接口（依赖 NavigationSystem）。
 */
UCLASS()
class SHOOT_API AGuestAIController : public AAIController
{
	GENERATED_BODY()

public:
	AGuestAIController();

protected:
	virtual void OnPossess(APawn* InPawn) override;

public:
	/** 前往指定世界坐标，到达后自动调用 Guest->OnReachLeavePoint() */
	void MoveToSeatLocation(const FVector& Target, AGuestCharacter* Guest);

	/** 移动到某个 Actor 的位置（座位），到达后自动调用 Guest->SitDown() */
	void MoveToSeatActor(AActor* Target, AGuestCharacter* Guest);

	/** 座位寻路最大重试次数（应对生成点尚未落地 / 导航未就绪导致的 Failed） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestAI")
	int32 MaxSeatMoveRetries = 5;

	/** 座位寻路每次重试的间隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestAI")
	float SeatMoveRetryInterval = 0.3f;

	/** 入座判定距离（cm）：与座位距离小于该值即视为到达并坐下 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestAI")
	float SeatArriveRadius = 120.0f;

	/** 入座“卡住吸附”距离（cm）：客人已停止移动（被桌子/碰撞挡住）且与座位小于该值，也视为到达并坐下。
	 *  用于应对终点被桌椅碰撞阻挡、导航投影点离座位较远的情况，避免客人停在桌边却坐不上去。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestAI")
	float SeatStuckRadius = 250.0f;

	/** 是否输出寻路/到达等调试信息（屏幕消息与 UE_LOG） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestAI|Debug")
	bool bShowDebugInfo = false;

	/** 徘徊到达判定距离（cm）：与徘徊点距离小于该值即视为到达 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GuestAI")
	float WanderAcceptRadius = 60.0f;

	/** 在 Center 附近 Radius 范围内开始随机徘徊（走走停停）。PauseMin/PauseMax 为每次驻足时长范围（秒） */
	void BeginWander(const FVector& Center, float Radius, float PauseMin, float PauseMax);

	/** 停止徘徊并停住移动（入座 / 离场前调用） */
	void StopWander();

private:
	/** 轮询到达状态的 Timer，便于到达或失效时清除 */
	FTimerHandle ArriveCheckTimerHandle;

	/** 座位寻路失败后的重试 Timer */
	FTimerHandle SeatRetryTimerHandle;

	/** 徘徊时轮询是否到达当前徘徊点的 Timer */
	FTimerHandle WanderCheckTimerHandle;

	/** 徘徊中"驻足/换点"的 Timer */
	FTimerHandle WanderPauseTimerHandle;

	/** 当前座位寻路已重试次数（每轮新寻路时重置） */
	int32 SeatMoveRetryCount = 0;

	/** 是否正在徘徊 */
	bool bWandering = false;

	/** 徘徊中心与半径 */
	FVector WanderCenter = FVector::ZeroVector;
	float WanderRadius = 250.0f;

	/** 每次驻足时长范围（秒） */
	float WanderPauseMin = 0.5f;
	float WanderPauseMax = 2.0f;

	/** 当前徘徊目标点 */
	FVector WanderTarget = FVector::ZeroVector;

	/** 挑选一个范围内可达的随机点并走过去 */
	void WanderToNextPoint();

	/** 执行一次座位寻路：成功（已接受/已到达）则开始轮询到达，失败则按配置延迟重试。返回是否成功发起 */
	bool AttemptMoveToSeat(AActor* Target, AGuestCharacter* Guest);
};
