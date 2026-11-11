#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "LaunchPad.generated.h"

class AShootCharacter;
class UNiagaraSystem;
class USoundBase;
class UBoxComponent;
class UStaticMeshComponent;

/**
 * 野外小径的弹跳蘑菇：玩家碰到顶部触发框时向上弹起。
 * 力度、冷却和压缩表现都在实例上可调。
 */
UCLASS(Blueprintable)
class SHOOT_API ALaunchPad : public AActor
{
	GENERATED_BODY()

public:
	ALaunchPad();

	/** 弹起时的向上速度（厘米/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LaunchPad")
	float LaunchVelocityZ = 1150.f;

	/** 两次触发之间的冷却（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LaunchPad")
	float CooldownSeconds = 0.8f;

	/** 压缩时 Z 轴相对缩放 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LaunchPad")
	float SquashScaleZ = 0.55f;

	/** 压缩恢复时间（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LaunchPad")
	float SquashDuration = 0.18f;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void Launch(AShootCharacter* Character);
	void ResetSquash();

	UPROPERTY(VisibleAnywhere, Category = "LaunchPad")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "LaunchPad")
	TObjectPtr<UStaticMeshComponent> PadMesh;

	UPROPERTY(VisibleAnywhere, Category = "LaunchPad")
	TObjectPtr<UBoxComponent> Trigger;

	UPROPERTY(EditAnywhere, Category = "LaunchPad|FX")
	TObjectPtr<USoundBase> LaunchSound;

	UPROPERTY(EditAnywhere, Category = "LaunchPad|FX")
	TObjectPtr<UNiagaraSystem> LaunchEffect;

private:
	double LastLaunchTime = -1000.0;
	FVector BaseMeshScale = FVector::OneVector;
	FTimerHandle SquashTimer;
};
