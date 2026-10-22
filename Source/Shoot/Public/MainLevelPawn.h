// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "MainLevelPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UStaticMeshComponent;
class UBoxComponent;
class UFloatingPawnMovement;
class ALevelActor;
struct FInputActionValue;

UCLASS()
class SHOOT_API AMainLevelPawn : public APawn
{
	GENERATED_BODY()

public:
	AMainLevelPawn();

protected:
	virtual void BeginPlay() override;
	
public:	
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// ========== 组件（参考 Cropout 镜头控制） ==========

	/** 相机摇臂 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** 跟随相机 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** 光标（3D 指示器） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Cursor;

	/** 静态网格体组件 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** 碰撞盒子 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> Collision;

	/** 悬浮移动组件（用于 AddMovementInput） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UFloatingPawnMovement> MovementComponent;

	// ========== 输入 ==========

	/** 键盘移动 Action（Axis2D 类型，绑定 WASD / 方向键） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	/** 交互按键 Action（绑定 E 键） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	// ========== 移动 ==========

	/** 移动速度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float MoveSpeed = 600.0f;

	/** 旋转插值速度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float RotationInterpSpeed = 10.0f;

	// ========== 悬停缩放 ==========

	/** 悬停时 Actor 的缩放倍数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlap")
	float HoverScale = 1.2f;

	/** 缩放动画插值速度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlap")
	float ScaleAnimInterpSpeed = 8.0f;

	// ========== Mesh Ping-Pong 缩放动画 ==========

	/** 是否启用 Mesh Ping-Pong 动画 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshPingPong")
	bool bEnableMeshPingPong = true;

	/** Ping-Pong 最小缩放 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshPingPong")
	float MeshPingPongMinScale = 0.9f;

	/** Ping-Pong 最大缩放 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshPingPong")
	float MeshPingPongMaxScale = 1.1f;

	/** Ping-Pong 动画速度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshPingPong")
	float MeshPingPongSpeed = 2.0f;

	/** Ping-Pong 累计时间 */
	float MeshPingPongTime = 0.0f;

	// ========== 地面跟随（Mesh 贴地） ==========

	/** 是否启用 Mesh 贴地移动 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GroundFollow")
	bool bEnableGroundFollow = true;

	/** 向下检测地面的距离 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GroundFollow")
	float GroundTraceDistance = 500.0f;

	/** 贴地高度偏移（正=离地更高） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GroundFollow")
	float GroundFollowOffset = 0.0f;

	/** 贴地平滑插值速度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GroundFollow")
	float GroundFollowInterpSpeed = 12.0f;

	/** 是否启用斜坡旋转对齐 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GroundFollow")
	bool bEnableSlopeAlign = true;

	/** 斜坡旋转对齐插值速度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GroundFollow")
	float SlopeRotationInterpSpeed = 8.0f;

protected:
	// ========== 输入回调 ==========

	/** 键盘移动输入 */
	UFUNCTION()
	void Move(const FInputActionValue& Value);

	/** E 键交互输入 */
	UFUNCTION()
	void Interact(const FInputActionValue& Value);

	// ========== 碰撞回调 ==========

	UFUNCTION()
	void OnCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnCollisionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** 每帧更新 Actor 缩放动画 */
	void UpdateActorScaleAnim(float DeltaTime);

	/** 每帧更新朝向（根据速度方向旋转） */
	void UpdateRotation(float DeltaTime);

	/** 每帧更新 Mesh Ping-Pong 缩放动画 */
	void UpdateMeshPingPong(float DeltaTime);

	/** 每帧更新 Mesh 贴地跟随 */
	void UpdateGroundFollow(float DeltaTime);

	/** 正在播放缩放动画的 Actor 及其目标缩放 */
	TMap<TWeakObjectPtr<AActor>, FVector> ActorTargetScales;

	/** 已悬停 Actor 的原始缩放记录 */
	TMap<TWeakObjectPtr<AActor>, FVector> ActorOriginalScales;

	/** 当前重叠的 LevelActor，用于 E 键交互 */
	TWeakObjectPtr<ALevelActor> CurrentOverlapLevelActor;
};
