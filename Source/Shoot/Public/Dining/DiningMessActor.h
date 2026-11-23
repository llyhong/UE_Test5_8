// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DiningMessActor.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class USceneComponent;
class AShootCharacter;

/** 就餐区散落物类型：垃圾 / 积水（白模阶段用引擎基础几何体区分） */
UENUM(BlueprintType)
enum class EDiningMessType : uint8
{
	Trash  UMETA(DisplayName = "垃圾"),
	Puddle UMETA(DisplayName = "积水"),
};

/** 被玩家清理掉（销毁前广播，管理器据此移除记录、停止扣分） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDiningMessCleaned, ADiningMessActor*, Mess);

/**
 * 就餐区散落物（垃圾 / 积水）。
 *
 * 实时玩法：由 ADiningHygieneManager 随机撒在就餐区；玩家靠近按 E 清理。
 * 长时间不清理由管理器统一扣声誉 / 压低顾客耐心（本 Actor 只记录存在时长 AgeSeconds）。
 * 外观交给蓝图子类或直接用本类（默认白模：垃圾=小方块，积水=压扁的圆柱）。
 */
UCLASS(Blueprintable)
class SHOOT_API ADiningMessActor : public AActor
{
	GENERATED_BODY()

public:
	ADiningMessActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 散落物类型：决定默认白模外观与提示文案 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningMess")
	EDiningMessType MessType = EDiningMessType::Trash;

	/** 生成时随机朝向与轻微缩放抖动，避免一排垃圾长得完全一样 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningMess")
	bool bRandomizeLooks = true;

	/** 玩家可清理距离（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningMess", meta = (ClampMin = "10"))
	float InteractionRadius = 160.f;

	/** 打印清理提示与调试日志 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|DiningMess|Debug")
	bool bShowDebugInfo = true;

	/** 已存在时长（秒），管理器据此判定“长时间未清理” */
	UFUNCTION(BlueprintPure, Category = "Shoot|DiningMess")
	float GetAgeSeconds() const { return AgeSeconds; }

	/** 该散落物的清理提示（垃圾=清理垃圾，积水=清理积水） */
	UFUNCTION(BlueprintPure, Category = "Shoot|DiningMess")
	FText GetCleanActionText() const;

	/** 切换类型并立即套用对应的白模外观（生成器在 SpawnActor 之后调用） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|DiningMess")
	void SetMessType(EDiningMessType NewType);

	/** 玩家按 E 清理：成功返回 true */
	UFUNCTION(BlueprintCallable, Category = "Shoot|DiningMess")
	bool TryClean(AShootCharacter* Player);

	/** 被清理时广播（销毁前） */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|DiningMess")
	FOnDiningMessCleaned OnCleaned;

	/** 清理表现（音效 / 粒子 / 飘字），在 Destroy 前调用 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|DiningMess")
	void BP_OnCleaned(AShootCharacter* Player);

protected:
	/** 场景根：网格与交互球都挂在它下面，网格才能用相对位置贴地而不覆盖 Actor 的生成位置 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|DiningMess|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** 白模外观（蓝图子类可换成正式资源；默认不参与碰撞） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|DiningMess|Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

	/** 玩家交互球（只做 Overlap 查询，不阻挡） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|DiningMess|Components")
	TObjectPtr<USphereComponent> InteractionSphere;

	UFUNCTION()
	void OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** 按 MessType 套用默认白模网格 / 缩放 / 离地高度（幂等，编辑器和运行时都会调用） */
	void ApplyTypeVisuals();

private:
	float AgeSeconds = 0.f;
	bool bCleaned = false;
};
