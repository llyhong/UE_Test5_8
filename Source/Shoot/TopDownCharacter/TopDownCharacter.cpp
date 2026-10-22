// Fill out your copyright notice in the Description page of Project Settings.


#include "TopDownCharacter/TopDownCharacter.h"

#include "EnhancedInputComponent.h"
#include "Shoot.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "UObject/ConstructorHelpers.h"
#include "Camera/CameraComponent.h"
#include "InputActionValue.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/Material.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"

// Sets default values
ATopDownCharacter::ATopDownCharacter()
{
 // Set size for player capsule
 	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
 
 	// Don't rotate character to camera direction
 	bUseControllerRotationPitch = false;
 	bUseControllerRotationYaw = false;
 	bUseControllerRotationRoll = false;
 
 	// Configure character movement
 	GetCharacterMovement()->bOrientRotationToMovement = true;
 	GetCharacterMovement()->RotationRate = FRotator(0.f, 640.f, 0.f);
 	GetCharacterMovement()->bConstrainToPlane = true;
 	GetCharacterMovement()->bSnapToPlaneAtStart = true;
 
 	// Create the camera boom component
 	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
 
 	CameraBoom->SetupAttachment(RootComponent);
 	CameraBoom->SetUsingAbsoluteRotation(true);
 	CameraBoom->TargetArmLength = 800.f;
 	CameraBoom->SetRelativeRotation(FRotator(-60.f, 0.f, 0.f));
 	CameraBoom->bDoCollisionTest = false;
 
 	// Create the camera component
 	TopDownCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
 
 	TopDownCameraComponent->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
 	TopDownCameraComponent->bUsePawnControlRotation = false;
 
 	// Activate ticking in order to update the cursor every frame.
 	PrimaryActorTick.bCanEverTick = true;
 	PrimaryActorTick.bStartWithTickEnabled = true;


	bIsTouch = false;
	bMoveToMouseCursor = false;

	// configure the controller
//	bShowMouseCursor = true;
//	DefaultMouseCursor = EMouseCursor::Default;
	CachedDestination = FVector::ZeroVector;
	FollowTime = 0.f;
}

// Called when the game starts or when spawned
void ATopDownCharacter::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ATopDownCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void ATopDownCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ATopDownCharacter::Move);

		// Setup mouse input events
		EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Started, this, &ATopDownCharacter::OnInputStarted);
		EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Triggered, this, &ATopDownCharacter::OnSetDestinationTriggered);
		EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Completed, this, &ATopDownCharacter::OnSetDestinationReleased);
		EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Canceled, this, &ATopDownCharacter::OnSetDestinationReleased);

		// Setup touch input events
		EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Started, this, &ATopDownCharacter::OnInputStarted);
		EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Triggered, this, &ATopDownCharacter::OnTouchTriggered);
		EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Completed, this, &ATopDownCharacter::OnTouchReleased);
		EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Canceled, this, &ATopDownCharacter::OnTouchReleased);
	}
	else
	{
		UE_LOG(LogShoot, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void ATopDownCharacter::OnInputStarted()
{
	if (Controller != nullptr)
	{
		Controller->StopMovement();
	}
}

void ATopDownCharacter::OnSetDestinationTriggered()
{
	// We flag that the input is being pressed
	FollowTime += GetWorld()->GetDeltaSeconds();

	APlayerController* PC = Cast<APlayerController>(Controller);
	if (PC != nullptr)
	{
		// We look for the location in the world where the player has pressed the input
		FHitResult Hit;
		bool bHitSuccessful = false;
		if (bIsTouch)
		{
			bHitSuccessful = PC->GetHitResultUnderFinger(ETouchIndex::Touch1, ECollisionChannel::ECC_Visibility, true, Hit);
		}
		else
		{
			bHitSuccessful = PC->GetHitResultUnderCursor(ECollisionChannel::ECC_Visibility, true, Hit);
		}

		// If we hit a surface, cache the location
		if (bHitSuccessful)
		{
			CachedDestination = Hit.Location;
		}
		
		// Move towards mouse pointer or touch
		FVector WorldDirection = (CachedDestination - GetActorLocation()).GetSafeNormal();
		AddMovementInput(WorldDirection, 1.0, false);
	}
	
}

void ATopDownCharacter::OnSetDestinationReleased()
{
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("OnSetDestinationReleased"));
	// If it was a short press
	if (FollowTime <= ShortPressThreshold && Controller != nullptr)
	{
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("SimpleMoveToLocation"));
		// If it was a short press
		// We move there and spawn some particles
		UAIBlueprintHelperLibrary::SimpleMoveToLocation(Controller, CachedDestination);
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, FXCursor, CachedDestination, FRotator::ZeroRotator, FVector(1.f, 1.f, 1.f), true, true, ENCPoolMethod::None, true);
	}

	FollowTime = 0.f;
}

void ATopDownCharacter::OnTouchTriggered()
{
	bIsTouch = true;
	OnSetDestinationTriggered();
}

void ATopDownCharacter::OnTouchReleased()
{
	bIsTouch = false;
	OnSetDestinationReleased();
}

void ATopDownCharacter::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

void ATopDownCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}


