// Fill out your copyright notice in the Description page of Project Settings.

#include "Machine/FirstDiscoveryCelebration.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 同一玩家只保留一份演出：连续解锁时直接替换，避免两层模型叠在头顶
	TMap<TWeakObjectPtr<ACharacter>, TWeakObjectPtr<AFirstDiscoveryCelebration>> GActiveCelebrations;
}

AFirstDiscoveryCelebration::AFirstDiscoveryCelebration()
{
	PrimaryActorTick.bCanEverTick = true;
	// 手部挂点跟随动画求值结果，放到物理之后采样才不会差一帧
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> AnimFinder(
		TEXT("/Game/CropCharacters/Animations/A_Excited.A_Excited"));
	CelebrationAnim = AnimFinder.Object;

	// 工程里现成的金币粒子，颜色和闪动节奏都适合做"发现"特效
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SparkleFinder(
		TEXT("/Game/StackOBot/FX/Coins/NS_CoinGet.NS_CoinGet"));
	SparkleSystem = SparkleFinder.Object;
}

void AFirstDiscoveryCelebration::SpawnForPlayer(ACharacter* Player, TSubclassOf<AActor> DishClass, float Duration)
{
	if (!Player || !Player->GetWorld())
	{
		return;
	}
	UWorld* World = Player->GetWorld();

	if (const TWeakObjectPtr<AFirstDiscoveryCelebration>* Existing = GActiveCelebrations.Find(Player))
	{
		if (Existing->IsValid())
		{
			(*Existing)->Destroy();
		}
	}

	FActorSpawnParameters Params;
	Params.Owner = Player;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AFirstDiscoveryCelebration* Celebration = World->SpawnActor<AFirstDiscoveryCelebration>(
		AFirstDiscoveryCelebration::StaticClass(),
		Player->GetActorLocation() + FVector(0.f, 0.f, 150.f),
		FRotator::ZeroRotator,
		Params);
	if (!Celebration)
	{
		return;
	}

	Celebration->TargetCharacter = Player;
	Celebration->TotalTime = FMath::Max(0.1f, Duration);
	Celebration->RemainTime = Celebration->TotalTime;
	GActiveCelebrations.Add(Player, Celebration);

	UE_LOG(LogTemp, Log, TEXT("[FirstDiscovery] 开始演出：玩家=%s 菜品=%s 时长=%.1fs"),
		*Player->GetName(), DishClass ? *DishClass->GetName() : TEXT("无"), Duration);

	if (DishClass)
	{
		FActorSpawnParameters DishParams;
		DishParams.Owner = Celebration;
		DishParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Celebration->DishActor = World->SpawnActor<AActor>(DishClass, Celebration->GetActorTransform(), DishParams);
		if (Celebration->DishActor)
		{
			// 成品模型只做展示：掉地上或挡住玩家都不行
			Celebration->DishActor->SetActorEnableCollision(false);
			Celebration->DishActor->DisableComponentsSimulatePhysics();
			Celebration->DishActor->AttachToActor(Celebration, FAttachmentTransformRules::KeepRelativeTransform);
			Celebration->DishActor->SetActorRelativeLocation(FVector::ZeroVector);
			Celebration->DishActor->SetActorRelativeRotation(FRotator::ZeroRotator);
			// 成品模型本身偏小，放大到接近参考图的展示体量
			Celebration->DishActor->SetActorRelativeScale3D(FVector(2.0f));
		}
	}

	if (Celebration->SparkleSystem)
	{
		Celebration->SparkleComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
			Celebration->SparkleSystem, Celebration->SceneRoot, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget, false);
		if (Celebration->SparkleComp)
		{
			Celebration->SparkleComp->SetRelativeScale3D(FVector(2.0f));
		}
	}

	// A_Excited 是 6 秒的庆祝动作，只借用前 3 秒；用 Slot 动画叠加，不打断角色原本的移动状态
	if (UAnimInstance* AnimInstance = Player->GetMesh() ? Player->GetMesh()->GetAnimInstance() : nullptr)
	{
		if (Celebration->CelebrationAnim)
		{
			Celebration->CelebrationMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
				Celebration->CelebrationAnim, FName("DefaultSlot"), 0.2f, 0.2f, 1.f, 1);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[FirstDiscovery] 庆祝动画=%s"),
		Celebration->CelebrationMontage ? TEXT("已播放") : TEXT("未播放"));

	// 首帧就放到展示位置，避免在炉子上方闪一下再飞过来
	FVector CameraLocation = Player->GetActorLocation() + FVector(0.f, 0.f, 300.f);
	if (const APlayerController* PC = Cast<APlayerController>(Player->GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
		}
	}
	Celebration->SetActorLocation(Celebration->CalcHoldLocation(Player, CameraLocation));
}

void AFirstDiscoveryCelebration::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ACharacter* Player = TargetCharacter.Get();
	if (!Player)
	{
		Finish();
		return;
	}

	FVector CameraLocation = Player->GetActorLocation() + FVector(0.f, 0.f, 300.f);
	float FaceYaw = GetActorRotation().Yaw;
	if (const APlayerController* PC = Cast<APlayerController>(Player->GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
			// 演出期间强制正面朝向镜头，玩家自己转视角也能看到举着菜的样子
			const FRotator FaceRot = (CameraLocation - Player->GetActorLocation()).Rotation();
			FaceYaw = FaceRot.Yaw;
			Player->SetActorRotation(FRotator(0.f, FaceYaw, 0.f));
		}
	}

	SetActorLocation(CalcHoldLocation(Player, CameraLocation) + FVector(0.f, 0.f, FMath::Sin((TotalTime - RemainTime) * 2.4f) * 5.f));
	// 演出体跟随玩家一起转向镜头，成品的 45° 前倾才有固定的"朝向"
	SetActorRotation(FRotator(0.f, FaceYaw, 0.f));

	SpinAngle = FMath::Fmod(SpinAngle + 60.f * DeltaTime, 360.f);
	if (DishActor)
	{
		// 先绕自身竖轴慢转，再整体前倾 45° 把顶面/正面亮给镜头
		const FQuat SpinQuat(FRotator(0.f, SpinAngle, 0.f));
		const FQuat TiltQuat(FRotator(-45.f, 0.f, 0.f));
		DishActor->SetActorRelativeRotation((TiltQuat * SpinQuat).Rotator());
	}

	RemainTime -= DeltaTime;
	if (RemainTime <= 0.f)
	{
		Finish();
	}
}

FVector AFirstDiscoveryCelebration::CalcHoldLocation(ACharacter* Player, const FVector& CameraLocation) const
{
	USkeletalMeshComponent* Mesh = Player ? Player->GetMesh() : nullptr;
	if (Player)
	{
		// 角色实际显示的是挂在 GetMesh 下的 RetargetMesh，手部挂点也配在它身上
		TArray<USkeletalMeshComponent*> Meshes;
		Player->GetComponents<USkeletalMeshComponent>(Meshes);
		for (USkeletalMeshComponent* Candidate : Meshes)
		{
			if (Candidate && Candidate->GetFName() == TEXT("RetargetMesh"))
			{
				Mesh = Candidate;
				break;
			}
		}
	}

	auto TrySocket = [Mesh](const FName& Name, FVector& Out) -> bool
	{
		if (!Mesh)
		{
			return false;
		}
		if (Mesh->DoesSocketExist(Name) || Mesh->GetBoneIndex(Name) != INDEX_NONE)
		{
			Out = Mesh->GetSocketLocation(Name);
			return true;
		}
		return false;
	};

	FVector Left, Right;
	const bool bHasLeft = TrySocket(FName("hand_lSocket"), Left) || TrySocket(FName("hand_l"), Left);
	const bool bHasRight = TrySocket(FName("hand_rSocket"), Right) || TrySocket(FName("hand_r"), Right);

	FVector Hold;
	FVector Head;
	if (TrySocket(FName("head"), Head))
	{
		// head 骨骼在颈部，抬高到头顶上方并留出空隙，俯视镜头下才不会像戴在头上
		Hold = Head + FVector(0.f, 0.f, 135.f);
	}
	else if (bHasLeft && bHasRight)
	{
		// 手部挂点在腕部，略抬高才落在双爪之间而不是贴在胸口
		Hold = (Left + Right) * 0.5f + FVector(0.f, 0.f, 18.f);
	}
	else if (bHasLeft)
	{
		Hold = Left + FVector(0.f, 0.f, 20.f);
	}
	else if (bHasRight)
	{
		Hold = Right + FVector(0.f, 0.f, 20.f);
	}
	else
	{
		Hold = Player ? Player->GetActorLocation() + FVector(0.f, 0.f, 200.f) : GetActorLocation();
	}

	// 俯视镜头下模型贴着身体会穿模，往镜头方向挪一点
	Hold += (CameraLocation - Hold).GetSafeNormal2D() * 30.f;
	return Hold;
}

void AFirstDiscoveryCelebration::Finish()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;

	UE_LOG(LogTemp, Log, TEXT("[FirstDiscovery] 演出结束（剩余 %.2fs）"), RemainTime);

	if (ACharacter* Player = TargetCharacter.Get())
	{
		if (UAnimInstance* AnimInstance = Player->GetMesh() ? Player->GetMesh()->GetAnimInstance() : nullptr)
		{
			if (CelebrationMontage && AnimInstance->Montage_IsPlaying(CelebrationMontage))
			{
				AnimInstance->Montage_Stop(0.25f, CelebrationMontage);
			}
		}
	}

	GActiveCelebrations.Remove(TargetCharacter);

	if (SparkleComp)
	{
		SparkleComp->DestroyComponent();
		SparkleComp = nullptr;
	}
	if (DishActor)
	{
		DishActor->Destroy();
		DishActor = nullptr;
	}

	Destroy();
}

void AFirstDiscoveryCelebration::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 外部销毁（切关卡 / 角色死亡）时补一次收尾，正常结束已在 Finish 里处理过
	if (!bFinished)
	{
		bFinished = true;
		if (ACharacter* Player = TargetCharacter.Get())
		{
			if (UAnimInstance* AnimInstance = Player->GetMesh() ? Player->GetMesh()->GetAnimInstance() : nullptr)
			{
				if (CelebrationMontage && AnimInstance->Montage_IsPlaying(CelebrationMontage))
				{
					AnimInstance->Montage_Stop(0.25f, CelebrationMontage);
				}
			}
		}
		GActiveCelebrations.Remove(TargetCharacter);
	}

	Super::EndPlay(EndPlayReason);
}
