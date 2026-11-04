// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Interaction/InteractionConfig.h"
#include "GameFramework/Actor.h"
#include "AttractCounter.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class AShootCharacter;
class AGuestManager;

/**
 * 招揽台：玩家靠近后由重叠球把自己注册到角色（AShootCharacter::OverlappingAttractCounter），
 * 在范围内按拾取键（PickAction，已与 AttractAction 合并）触发招揽。
 * 招揽不再直接生成客人，而是提升声誉值；声誉越高，GuestManager 的容量上限(GetEffectiveMaxGuests)
 * 越高，自动生成(SpawnInterval)会把在场客人补满到新上限，从而实现“声誉高则客人多”。
 */
UCLASS(Blueprintable)
class SHOOT_API AAttractCounter : public AActor
{
	GENERATED_BODY()

public:
	AAttractCounter();

	virtual void BeginPlay() override;

	/** 外观模型 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* CounterMesh;

	/** 交互范围：玩家进入后注册为可招揽 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USphereComponent* InteractionSphere;

	// ============ 交互判定尺度（统一标准，见 InteractionConfig.h） ============
	/** 交互判定半径。C++ 在 OnConstruction（编辑器视口）与 BeginPlay（运行时）都会强制应用，
	 *  会覆盖组件上残留的旧值；要单独调这台设备就改这里，不要直接改组件上的半径 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "10.0"))
	float InteractionRadius = InteractionDistance::Attract;

	/** 判定球心相对 Actor 原点的高度：抬到物体中部，否则贴地的球只在脚踝高度有判定 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	float InteractionCenterHeight = InteractionDistance::CenterHeight;

	/** 编辑器里改上面的值时立即写进组件，关卡视口中即可看到真实判定球 */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 把 InteractionRadius（× Shoot.InteractScale 倍率）写进交互组件；OnConstruction/BeginPlay/运行时改倍率都会调用，必须幂等 */
	UFUNCTION()
	void ApplyInteractionScale();

	/** 每次招揽基础提升的声誉值（声誉=0 时每次按键 +BaseReputationGain） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attract")
	int32 BaseReputationGain = 2;

	/** 单次按键提升声誉的上限（防止数值爆炸） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attract")
	int32 MaxReputationGainPerPress = 20;

	/** 每持有 1 个“霓虹招牌”(道具ID=1003) 额外提升的声誉值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attract")
	int32 ExtraReputationPerSign = 3;

	/** 招揽冷却（秒），避免按住 E 疯狂刷客 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attract")
	float AttractCooldown = 2.0f;

	/** 招揽：提升声誉值（不再直接生成客人）；声誉越高，GuestManager 容量上限越高、自动生成补满客人 */
	UFUNCTION(BlueprintCallable, Category = "Attract")
	void AttractCustomers();

	/** 招揽成功通知（蓝图可重载：播放特效 / 音效），ReputationGain 为本轮提升的声誉值 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Attract")
	void OnAttract(int32 ReputationGain);

	UFUNCTION()
	void OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

protected:
	/** 在关卡中寻找 GuestManager（找不到返回 nullptr） */
	AGuestManager* FindGuestManager() const;

	float LastAttractTime = -999.f;
};
