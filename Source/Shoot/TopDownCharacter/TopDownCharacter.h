// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TopDownCharacter.generated.h"

class UNiagaraSystem;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

UCLASS(abstract)
class SHOOT_API ATopDownCharacter : public ACharacter
{
	GENERATED_BODY()
private:
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	class UCameraComponent* TopDownCameraComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	class USpringArmComponent* CameraBoom;
	
public:
	ATopDownCharacter();

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;

	FORCEINLINE class UCameraComponent* GetTopDownCameraComponent() const { return TopDownCameraComponent; }

	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

protected:

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	UPROPERTY(EditAnywhere, Category="Input")
	UNiagaraSystem* FXCursor;
	
	UPROPERTY(EditAnywhere, Category="Input")
	float ShortPressThreshold;
	
	UPROPERTY(EditAnywhere, Category="Input")
	UInputMappingContext* DefaultMappingContext;
	
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* SetDestinationClickAction;

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* SetDestinationTouchAction;
	
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	uint32 bMoveToMouseCursor : 1;

	uint32 bIsTouch : 1;

	FVector CachedDestination;

	float FollowTime = 0.0f;

	
	/** Input handlers */
	void OnInputStarted();
	void OnSetDestinationTriggered();
	void OnSetDestinationReleased();
	void OnTouchTriggered();
	void OnTouchReleased();
	void Move(const FInputActionValue& Value);
};
