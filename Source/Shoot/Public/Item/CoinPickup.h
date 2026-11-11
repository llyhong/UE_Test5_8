#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "CoinPickup.generated.h"

class AShootCharacter;
class UNiagaraSystem;
class USoundBase;
class USphereComponent;
class UStaticMeshComponent;

/**
 * 野外小径的金币：碰到即收取，默认按秒重生。
 * 金币价值、重生时间和漂浮参数都在实例上可调。
 */
UCLASS(Blueprintable)
class SHOOT_API ACoinPickup : public AActor
{
	GENERATED_BODY()

public:
	ACoinPickup();

	virtual void Tick(float DeltaSeconds) override;

	/** 拾取后获得的金钱 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coin")
	int32 CoinValue = 2;

	/** 收取后多少秒重新出现；0 表示本次游戏内只收一次 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coin")
	float RespawnSeconds = 180.f;

	/** 自转速度（度/秒），0 为不转 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coin")
	float SpinDegreesPerSecond = 120.f;

	/** 上下漂浮幅度（厘米） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coin")
	float BobAmplitude = 10.f;

	/** 上下漂浮速度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coin")
	float BobSpeed = 2.f;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void Collect(AShootCharacter* Character);
	void RespawnCoin();

	UPROPERTY(VisibleAnywhere, Category = "Coin")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Coin")
	TObjectPtr<UStaticMeshComponent> CoinMesh;

	UPROPERTY(VisibleAnywhere, Category = "Coin")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(EditAnywhere, Category = "Coin|FX")
	TObjectPtr<USoundBase> CollectSound;

	UPROPERTY(EditAnywhere, Category = "Coin|FX")
	TObjectPtr<UNiagaraSystem> CollectEffect;

private:
	bool bCollected = false;
	FVector BaseLocation = FVector::ZeroVector;
	float BobPhase = 0.f;
	FTimerHandle RespawnHandle;
};
