#pragma once

#include "CoreMinimal.h"
#include "Interaction/InteractionConfig.h"
#include "GameFramework/Actor.h"
#include "GateCounter.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class ULocalPlayer;
class AShootCharacter;
class UTimeSystem;

/**
 * 大门交互台：玩家靠近后由重叠球注册到角色（AShootCharacter::OverlappingGate），
 * 在范围内按拾取键（PickAction）触发开门营业（调用 UTimeSystem::OpenBusiness）。
 * 仅在营业时段（OpenHour~CloseHour，默认傍晚 17 点 ~ 晚上 22 点）内开门才成功，
 * 否则提示“未到营业时间”。开门后场景灯饰点亮、客人开始到来。
 */
UCLASS(Blueprintable)
class SHOOT_API AGateCounter : public AActor
{
	GENERATED_BODY()

public:
	AGateCounter();

	virtual void BeginPlay() override;

	/** 外观模型（门 / 招牌等），蓝图可替换 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> GateMesh;

	/** 交互范围：玩家进入后注册为可开门 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> InteractionSphere;

	// ============ 交互判定尺度（统一标准，见 InteractionConfig.h） ============
	/** 交互判定半径。C++ 在 OnConstruction（编辑器视口）与 BeginPlay（运行时）都会强制应用，
	 *  会覆盖组件上残留的旧值；要单独调这台设备就改这里，不要直接改组件上的半径 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "10.0"))
	float InteractionRadius = InteractionDistance::Gate;

	/** 判定球心相对 Actor 原点的高度：抬到物体中部，否则贴地的球只在脚踝高度有判定 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	float InteractionCenterHeight = InteractionDistance::CenterHeight;

	/** 编辑器里改上面的值时立即写进组件，关卡视口中即可看到真实判定球 */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 把 InteractionRadius（× Shoot.InteractScale 倍率）写进交互组件；OnConstruction/BeginPlay/运行时改倍率都会调用，必须幂等 */
	UFUNCTION()
	void ApplyInteractionScale();

	/** 交互提示控件（如“按 E 开门营业”）：靠近时显示，离开隐藏；蓝图可在组件上配置 Widget Class */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWidgetComponent> TipsWidget;

	/** 调试：靠近时打印重叠注册信息，便于排查“靠近没反应” */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate|Debug")
	bool bDebug = false;

	/** 玩家按 E 开门营业：成功返回 true（已营业也返回 true），未到营业时间返回 false */
	UFUNCTION(BlueprintCallable, Category = "Gate")
	bool OpenBusiness();

	/** 开门结果通知（蓝图可播放特效 / 音效 / 飘字）：bSuccess=true 已开始营业，false 未到营业时间 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Gate")
	void OnOpenResult(bool bSuccess);

	UFUNCTION()
	void OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

protected:
	/** 经 GameInstance->businessSimulation 取 TimeSystem；取不到返回 nullptr */
	UTimeSystem* GetTimeSystem() const;

	/** 当前占用提示控件的本地玩家（多人分屏隔离用，避免提示被其它玩家误显示） */
	UPROPERTY()
	TObjectPtr<ULocalPlayer> LocalPlayer;
};
