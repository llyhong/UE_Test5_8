// 动物动画实例：根据 AAnimalCharacter 的状态驱动动画
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "AnimalTypes.h"
#include "AnimalAnimation.generated.h"

class AAnimalCharacter;

UCLASS()
class SHOOT_API UAnimalAnimation : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	// 由 AnimBP 状态机读取的动画状态
	UPROPERTY(BlueprintReadOnly, Category = "AnimalAnim")
	bool bIsIdle = false;

	UPROPERTY(BlueprintReadOnly, Category = "AnimalAnim")
	bool bIsWalking = false;

	UPROPERTY(BlueprintReadOnly, Category = "AnimalAnim")
	bool bIsRunning = false;

	UPROPERTY(BlueprintReadOnly, Category = "AnimalAnim")
	bool bIsEating = false;

	UPROPERTY(BlueprintReadOnly, Category = "AnimalAnim")
	float Speed = 0.f; // 当前水平速度

	// 子物种类型：AnimBP 中可用作 blend 变量，区分鹿/鸡/马的不同骨骼/动画
	UPROPERTY(BlueprintReadOnly, Category = "AnimalAnim")
	EAnimalSpecies Species = EAnimalSpecies::Deer;

protected:
	UPROPERTY()
	AAnimalCharacter* OwnerAnimal = nullptr; // 缓存的所属动物

	// 走/跑速度分界：与 AnimalCharacter 区间对齐(行走 1~300，跑步 301~600)
	// 速度 <=300 为走，>300 为跑(蓝图可覆盖)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AnimalAnim")
	float WalkSpeedThreshold = 300.f;
};
