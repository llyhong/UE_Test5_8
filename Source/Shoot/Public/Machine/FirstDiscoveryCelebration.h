// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FirstDiscoveryCelebration.generated.h"

class ACharacter;
class USceneComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UAnimSequenceBase;
class UAnimMontage;
class USkeletalMeshComponent;

/**
 * 首次发现演出：研究出新菜谱时，让玩家面向镜头、举起双手，
 * 成品模型悬浮在头顶上方旋转展示，持续数秒后自动消失（huanglily）。
 * 由 AResearchStove::FinishResearch 为每个本地玩家各生成一份，不保存到存档。
 */
UCLASS()
class SHOOT_API AFirstDiscoveryCelebration : public AActor
{
	GENERATED_BODY()

public:
	AFirstDiscoveryCelebration();

	/** 为指定玩家播放一次发现演出；同一玩家重复触发时替换上一次（避免叠两层） */
	static void SpawnForPlayer(ACharacter* Player, TSubclassOf<AActor> DishClass, float Duration = 3.f);

	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	/** 展示用的成品模型（从菜品行 CookedActorClass 生成，关闭碰撞与物理） */
	UPROPERTY()
	TObjectPtr<AActor> DishActor;

	/** 成品周围的金色粒子（复用金币特效做 blingbling） */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> SparkleComp;

	UPROPERTY()
	TWeakObjectPtr<ACharacter> TargetCharacter;

	UPROPERTY()
	TObjectPtr<UAnimSequenceBase> CelebrationAnim;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> SparkleSystem;

	UPROPERTY()
	TObjectPtr<UAnimMontage> CelebrationMontage;

	float RemainTime = 0.f;
	float TotalTime = 0.f;
	float SpinAngle = 0.f;
	bool bFinished = false;

	/** 以头顶挂点为锚点算展示位置；挂点缺失时逐级回退到双手中点 / 单手 / 角色原点上方 */
	FVector CalcHoldLocation(ACharacter* Player, const FVector& CameraLocation) const;

	void Finish();
};
