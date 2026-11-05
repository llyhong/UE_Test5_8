// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "ShootCharacter.h"
#include "ShootAnimInstance.generated.h"

/**
 * 
 */
UCLASS()
class SHOOT_API UShootAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Animation")
	class AShootCharacter* ShootCharacter;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Movement")
	class UCharacterMovementComponent* ShootCharacterMovement;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Movement")
	float GroundSpeed;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Movement")
	bool IsFalling;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Movement")
	ECharacterState CharacterState;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Movement")
	ECharacterActionState ActionState;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|IK")
	float FBIK_Alpha;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|IK")
	FTransform HandIK_L;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|IK")
	FTransform HandIK_R;
};
