// Fill out your copyright notice in the Description page of Project Settings.


#include "Animation/ShootAnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"

void UShootAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	ShootCharacter = Cast<AShootCharacter>(TryGetPawnOwner());
	if (ShootCharacter)
	{
		ShootCharacterMovement = ShootCharacter->GetCharacterMovement();
	}
}

void UShootAnimInstance::NativeUpdateAnimation(float DeltaTime)
{
	Super::NativeUpdateAnimation(DeltaTime);
	if (ShootCharacterMovement)
	{
		GroundSpeed = UKismetMathLibrary::VSizeXY(ShootCharacterMovement->Velocity);
		IsFalling = ShootCharacterMovement->IsFalling();
		CharacterState = ShootCharacter->GetCharacterState();
		ActionState = ShootCharacter->GetActionState();
	//	DeathPose = SlashCharacter->GetDeathPose();
     	
	}
}
