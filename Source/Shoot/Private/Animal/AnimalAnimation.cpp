// 动物动画实例实现
#include "Animal/AnimalAnimation.h"
#include "Animal/AnimalCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

void UAnimalAnimation::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	OwnerAnimal = Cast<AAnimalCharacter>(TryGetPawnOwner());
}

void UAnimalAnimation::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!OwnerAnimal)
	{
		OwnerAnimal = Cast<AAnimalCharacter>(TryGetPawnOwner());
	}
	if (!OwnerAnimal)
	{
		return;
	}

	// 水平速度
	if (const UCharacterMovementComponent* Mov = OwnerAnimal->GetCharacterMovement())
	{
		Speed = Mov->Velocity.Size2D();
	}
	else
	{
		Speed = 0.f;
	}

	// 吃动画：由 AnimalCharacter 的 bIsEating 直接驱动(需求满足时置位)
	bIsEating = OwnerAnimal->bIsEating;

	// 子物种类型：同步给 AnimBP 用于 blend（鹿/鸡/马）
	Species = OwnerAnimal->Species;

	// 休息/未激活 AI 时视为 Idle
	const bool bShouldIdle = OwnerAnimal->bResting || !OwnerAnimal->bAIActive;

	if (bIsEating)
	{
		// 进食优先，其它移动状态关闭
		bIsIdle = false;
		bIsWalking = false;
		bIsRunning = false;
	}
	else if (bShouldIdle || Speed < 10.f)
	{
		bIsIdle = true;
		bIsWalking = false;
		bIsRunning = false;
	}
	else if (Speed >= WalkSpeedThreshold)
	{
		bIsIdle = false;
		bIsWalking = false;
		bIsRunning = true;
	}
	else
	{
		bIsIdle = false;
		bIsWalking = true;
		bIsRunning = false;
	}
}
