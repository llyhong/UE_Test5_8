// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Item/ItemActor.h"
#include "Weapon.generated.h"

class UBoxComponent;
class AAnimalCharacter;
class AShootCharacter;
/**
 * 
 */
UCLASS()
class SHOOT_API AWeapon : public AItemActor
{
	GENERATED_BODY()
public:
	AWeapon();

	TArray<AActor*> IgnoreActors;
	
protected:
	virtual void BeginPlay() override;
	
	UFUNCTION()
	void OnBoxOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void ExecuteGetHit(FHitResult& BoxHit);

	bool ActorIsSameType(AActor* OtherActor);
	
private:
	void BoxTrace(FHitResult& BoxHit);
	
	UPROPERTY(VisibleAnywhere, Category = "Weapon Properties")
	UBoxComponent* WeaponBox;

	UPROPERTY(EditAnywhere, Category = "Weapon Properties")
	FVector BoxTraceExtent = FVector(5.f);

	UPROPERTY(EditAnywhere, Category = "Weapon Properties")
	bool bShowBoxDebug = false;
	
	UPROPERTY(VisibleAnywhere)
	USceneComponent* BoxTraceStart;

	UPROPERTY(VisibleAnywhere)
	USceneComponent* BoxTraceEnd;

	UPROPERTY(EditAnywhere, Category = "Weapon Properties")
	float Damage = 20.f;
	
public:
	FORCEINLINE UBoxComponent* GetWeaponBox() const { return WeaponBox; }

	// 本次攻击造成的基础伤害(供攻击方结算血量时使用)
	FORCEINLINE float GetDamage() const { return Damage; }

	// 攻击命中动物：读取动物当前血量，耗尽则触发 Die，否则扣血(更新血量)。
	// 仅当持有者处于攻击状态时结算(武器碰撞箱只在攻击时启用，这里再兜底校验)。
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void DealDamageToAnimal(AAnimalCharacter* Animal, const FVector& ImpactPoint);
};
