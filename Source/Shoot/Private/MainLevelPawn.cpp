// Fill out your copyright notice in the Description page of Project Settings.


#include "MainLevelPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/MenuData.h"
#include "Game/LevelGoalData.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Level/LevelActor.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"

AMainLevelPawn::AMainLevelPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	
	// —— 碰撞盒子（根组件） ——
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->SetBoxExtent(FVector(180.0f, 180.0f, 180.0f));
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetCollisionObjectType(ECC_Pawn);
	Collision->SetCollisionResponseToAllChannels(ECR_Overlap);
	Collision->SetGenerateOverlapEvents(true);
	SetRootComponent(Collision);
	
	// —— 相机摇臂（参考 Cropout 俯视角视角） ——
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(Collision);
	CameraBoom->TargetArmLength = 1200.0f;
	CameraBoom->SetRelativeRotation(FRotator(-60.0f, 0.0f, 0.0f)); // 俯视角
	CameraBoom->bDoCollisionTest = false;                            // 不进行碰撞检测
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bInheritRoll = false;

	// —— 镜头平滑跟随 ——
	CameraBoom->bEnableCameraLag = true;       // 位置延迟
	CameraBoom->CameraLagSpeed = 8.0f;          // 延迟跟随速度
	CameraBoom->bEnableCameraRotationLag = true;// 旋转延迟
	CameraBoom->CameraRotationLagSpeed = 10.0f;

	// —— 跟随相机 ——
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// —— 光标（3D 指示器） ——
	Cursor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cursor"));
	Cursor->SetupAttachment(Collision);
	Cursor->SetCastShadow(false);
	Cursor->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// —— 静态网格体组件 ——
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(Collision);

	// —— 悬浮移动组件 ——
	MovementComponent = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("MovementComponent"));
	MovementComponent->UpdatedComponent = Collision;

}

void AMainLevelPawn::BeginPlay()
{
	Super::BeginPlay();

	// 绑定碰撞重叠事件
	if (Collision)
	{

		Collision->OnComponentBeginOverlap.AddDynamic(this, &AMainLevelPawn::OnCollisionBeginOverlap);
		Collision->OnComponentEndOverlap.AddDynamic(this, &AMainLevelPawn::OnCollisionEndOverlap);
	}
}

void AMainLevelPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// —— 根据速度方向旋转朝向 ——
	UpdateRotation(DeltaTime);

	// —— 更新悬停缩放动画 ——
	UpdateActorScaleAnim(DeltaTime);

	// —— 更新 Mesh Ping-Pong 动画 ——
	UpdateMeshPingPong(DeltaTime);

	// —— 更新 Mesh 贴地跟随 ——
	//UpdateGroundFollow(DeltaTime);
}

// ========== 输入 ==========

void AMainLevelPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMainLevelPawn::Move);
		EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &AMainLevelPawn::Interact);
	}
}

void AMainLevelPawn::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();

	// 俯视角：Y 轴（前/后）→ 世界 X，X 轴（右/左）→ 世界 Y
	AddMovementInput(FVector::ForwardVector, MovementVector.Y);
	AddMovementInput(FVector::RightVector, MovementVector.X);
}

void AMainLevelPawn::Interact(const FInputActionValue& Value)
{
	if (ALevelActor* LevelActor = CurrentOverlapLevelActor.Get())
	{
		// 从 DataTable 获取 FLevelGoalRow（已合并原 FLevelInfo 的关卡入口字段）
	 	const FLevelGoalRow* LevelInfo = LevelActor->LevelDataTableRow.GetRow<FLevelGoalRow>(TEXT("LevelInfo"));
	 	if (LevelInfo && !LevelInfo->LevelPath.IsEmpty())
	 	{
	 		UGameplayStatics::OpenLevel(this, FName(*LevelInfo->LevelPath));
	 	}
	}
}
// ========== 碰撞重叠 → ALevelActor 缩放 ==========

void AMainLevelPawn::OnCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || OtherActor == this) return;

	if (ALevelActor* LevelActor = Cast<ALevelActor>(OtherActor))
	{
		// 记录当前重叠的 LevelActor
		CurrentOverlapLevelActor = LevelActor;

		// 记录原始缩放并设置放大目标
		const FVector OriginalScale = LevelActor->GetActorScale3D();
		ActorOriginalScales.Add(LevelActor, OriginalScale);
		ActorTargetScales.Add(LevelActor, OriginalScale * HoverScale);

		// 触发蓝图事件 & 显示 Widget
		LevelActor->OnBeginCursorOverlap();
		LevelActor->ShowWidget();
	}
}

void AMainLevelPawn::OnCollisionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!OtherActor) return;

	if (ALevelActor* LevelActor = Cast<ALevelActor>(OtherActor))
	{
		// 清除当前重叠的 LevelActor
		if (CurrentOverlapLevelActor.Get() == LevelActor)
		{
			CurrentOverlapLevelActor.Reset();
		}

		// 触发蓝图事件 & 隐藏 Widget
		LevelActor->OnEndCursorOverlap();
		LevelActor->HideWidget();

		// 恢复原始缩放
		if (const FVector* OriginalScale = ActorOriginalScales.Find(LevelActor))
		{
			ActorTargetScales.Add(LevelActor, *OriginalScale);
		}
		ActorOriginalScales.Remove(LevelActor);
	}
}

// ========== 缩放动画插值 ==========

void AMainLevelPawn::UpdateRotation(float DeltaTime)
{
	const FVector Velocity = GetVelocity();

	// 速度太小不旋转
	if (Velocity.SizeSquared2D() < 1.0f) return;

	const FRotator TargetRotation = Velocity.Rotation();
	const FRotator CurrentRotation = GetActorRotation();
	const FRotator NewRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, RotationInterpSpeed);
	SetActorRotation(NewRotation);
}

void AMainLevelPawn::UpdateActorScaleAnim(float DeltaTime)
{
	TArray<AActor*> CompletedActors;

	for (auto& Pair : ActorTargetScales)
	{
		AActor* Actor = Pair.Key.Get();
		if (!Actor)
		{
			CompletedActors.Add(nullptr);
			continue;
		}

		const FVector TargetScale = Pair.Value;
		const FVector CurrentScale = Actor->GetActorScale3D();
		const FVector NewScale = FMath::VInterpTo(CurrentScale, TargetScale, DeltaTime, ScaleAnimInterpSpeed);

		Actor->SetActorScale3D(NewScale);

		if (NewScale.Equals(TargetScale, 0.001f))
		{
			Actor->SetActorScale3D(TargetScale);
			CompletedActors.Add(Actor);
		}
	}

	for (AActor* CompletedActor : CompletedActors)
	{
		for (auto It = ActorTargetScales.CreateIterator(); It; ++It)
		{
			if (It.Key().Get() == CompletedActor)
			{
				It.RemoveCurrent();
				break;
			}
		}
	}
}

void AMainLevelPawn::UpdateMeshPingPong(float DeltaTime)
{
	if (!bEnableMeshPingPong || !MeshComponent) return;

	const float Center = (MeshPingPongMaxScale + MeshPingPongMinScale) * 0.5f;

	// 仅移动时播放动画，静止时恢复中心缩放
	//if (GetVelocity().SizeSquared2D() < 1.0f)
	//{
	//	MeshComponent->SetWorldScale3D(FVector(Center));
	//	return;
	//}

	MeshPingPongTime += DeltaTime;

	const float HalfRange = (MeshPingPongMaxScale - MeshPingPongMinScale) * 0.5f;
	const float T = FMath::Sin(MeshPingPongTime * MeshPingPongSpeed);
	const float ScaleValue = Center + HalfRange * T;

	MeshComponent->SetWorldScale3D(FVector(ScaleValue));
}

void AMainLevelPawn::UpdateGroundFollow(float DeltaTime)
{
	if (!bEnableGroundFollow || !MeshComponent) return;

	const FVector PawnLoc = GetActorLocation();
	const FVector TraceStart = PawnLoc;
	const FVector TraceEnd = PawnLoc - FVector(0.0f, 0.0f, GroundTraceDistance);

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
	{
		// —— 高度贴地 ——
		const float TargetRelativeZ = Hit.ImpactPoint.Z - PawnLoc.Z + GroundFollowOffset;

		const FVector CurrentRelativeLoc = MeshComponent->GetRelativeLocation();
		const float NewZ = FMath::FInterpTo(CurrentRelativeLoc.Z, TargetRelativeZ, DeltaTime, GroundFollowInterpSpeed);

		MeshComponent->SetRelativeLocation(FVector(CurrentRelativeLoc.X, CurrentRelativeLoc.Y, NewZ));

		// —— 斜坡旋转对齐 ——
		if (bEnableSlopeAlign)
		{
			// 构造世界空间下对齐到斜面的旋转
			FRotator SlopeWorldRotation = FRotationMatrix::MakeFromZ(Hit.Normal).Rotator();
			// Yaw 由 Actor（Collision）的朝向控制，不被斜面覆盖
			SlopeWorldRotation.Yaw = GetActorRotation().Yaw;

			// 转为相对 Collision（根组件）的局部旋转
			const FQuat ActorQuat = GetActorQuat();
			const FQuat SlopeQuat = SlopeWorldRotation.Quaternion();
			const FQuat TargetRelativeQuat = ActorQuat.Inverse() * SlopeQuat;

			const FQuat CurrentRelativeQuat = MeshComponent->GetRelativeRotation().Quaternion();
			const FQuat NewRelativeQuat = FMath::QInterpTo(
				CurrentRelativeQuat, TargetRelativeQuat, DeltaTime, SlopeRotationInterpSpeed);

			MeshComponent->SetRelativeRotation(NewRelativeQuat);
		}
	}
	else
	{
		// 无地面 → 平滑回到相对 Z = 0、相对旋转归零
		const FVector CurrentRelativeLoc = MeshComponent->GetRelativeLocation();
		const float NewZ = FMath::FInterpTo(CurrentRelativeLoc.Z, 0.0f, DeltaTime, GroundFollowInterpSpeed);
		MeshComponent->SetRelativeLocation(FVector(CurrentRelativeLoc.X, CurrentRelativeLoc.Y, NewZ));

		if (bEnableSlopeAlign)
		{
			const FQuat CurrentRelativeQuat = MeshComponent->GetRelativeRotation().Quaternion();
			const FQuat NewRelativeQuat = FMath::QInterpTo(
				CurrentRelativeQuat, FQuat::Identity, DeltaTime, SlopeRotationInterpSpeed);
			MeshComponent->SetRelativeRotation(NewRelativeQuat);
		}
	}
}



