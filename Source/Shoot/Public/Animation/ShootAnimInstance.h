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

	UPROPERTY(BlueprintReadOnly)
	class AShootCharacter* ShootCharacter;

	UPROPERTY(BlueprintReadOnly, Category = Movement)
	class UCharacterMovementComponent* ShootCharacterMovement;

	UPROPERTY(BlueprintReadOnly, Category = Movement)
	float GroundSpeed;

	UPROPERTY(BlueprintReadOnly, Category = Movement)
	bool IsFalling;

	UPROPERTY(BlueprintReadOnly, Category = Movement)
	ECharacterState CharacterState;

	UPROPERTY(BlueprintReadOnly, Category = Movement)
	ECharacterActionState ActionState;

	UPROPERTY(BlueprintReadOnly, Category = IK)
	float FBIK_Alpha;

	UPROPERTY(BlueprintReadOnly, Category = IK)
	FTransform HandIK_L;

	UPROPERTY(BlueprintReadOnly, Category = IK)
	FTransform HandIK_R;
};
