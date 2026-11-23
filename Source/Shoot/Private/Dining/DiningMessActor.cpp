// Fill out your copyright notice in the Description page of Project Settings.

#include "Dining/DiningMessActor.h"
#include "Dining/DiningHygieneManager.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "ShootCharacter.h"
#include "UObject/ConstructorHelpers.h"

ADiningMessActor::ADiningMessActor()
{
	PrimaryActorTick.bCanEverTick = true;
	// 只用 Tick 累计存在时长（扣分节奏由管理器控制），不必每帧都跑表现逻辑
	PrimaryActorTick.TickInterval = 0.25f;

	// 场景根：网格必须挂在子级，否则网格的相对位置会直接改写 Actor 的生成位置
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// 白模网格：不参与碰撞，不挡玩家 / 不挡老鼠
	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetupAttachment(SceneRoot);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComp->SetGenerateOverlapEvents(false);
	MeshComp->SetCastShadow(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		MeshComp->SetStaticMesh(CubeMesh.Object);
	}
	MeshComp->SetRelativeScale3D(FVector(0.28f, 0.22f, 0.2f));

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(SceneRoot);
	InteractionSphere->InitSphereRadius(InteractionRadius);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractionSphere->SetGenerateOverlapEvents(true);

	// 编辑器预览 / 运行时都先套用一次默认白模（OnConstruction 里按类型再覆盖）
	ApplyTypeVisuals();
}

void ADiningMessActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyTypeVisuals();
	if (bRandomizeLooks && GetWorld() && GetWorld()->IsGameWorld())
	{
		AddActorLocalRotation(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f));
		const float Jitter = FMath::FRandRange(0.85f, 1.2f);
		MeshComp->SetRelativeScale3D(MeshComp->GetRelativeScale3D() * FVector(Jitter, Jitter, 1.f));
	}
}

void ADiningMessActor::BeginPlay()
{
	Super::BeginPlay();
	// Also cover level-placed messes or spawns that bypass the hygiene manager.
	if (!ADiningHygieneManager::IsMessDayAllowed(this))
	{
		Destroy();
		return;
	}

	InteractionSphere->SetSphereRadius(InteractionRadius);
	InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &ADiningMessActor::OnInteractionOverlap);
	InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &ADiningMessActor::OnInteractionEndOverlap);
	TArray<AActor*> Players;
	InteractionSphere->GetOverlappingActors(Players, AShootCharacter::StaticClass());
	for (AActor* Player : Players)
	{
		CastChecked<AShootCharacter>(Player)->SetOverlappingMess(this);
	}
}

void ADiningMessActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!ADiningHygieneManager::IsMessDayAllowed(this))
	{
		Destroy();
		return;
	}

	// DeltaSeconds 仍是距上次 Tick 的真实时长（TickInterval 只改变调用频率）
	if (const auto* Manager = Cast<ADiningHygieneManager>(GetOwner()); !Manager || Manager->IsHygieneActive())
	{
		AgeSeconds += FMath::Max(0.f, DeltaSeconds);
	}
}

void ADiningMessActor::ApplyTypeVisuals()
{
	if (!MeshComp)
	{
		return;
	}

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	if (MessType == EDiningMessType::Puddle)
	{
		if (Cylinder)
		{
			MeshComp->SetStaticMesh(Cylinder);
		}
		// 压扁成一片水渍，贴地略抬避免 z-fighting
		MeshComp->SetRelativeScale3D(FVector(1.5f, 1.5f, 0.012f));
		MeshComp->SetRelativeLocation(FVector(0.f, 0.f, 0.6f));
	}
	else
	{
		if (Cube)
		{
			MeshComp->SetStaticMesh(Cube);
		}
		MeshComp->SetRelativeScale3D(FVector(0.28f, 0.22f, 0.2f));
		// 方块中心抬到半高，让底部贴地而不是一半埋进地板
		MeshComp->SetRelativeLocation(FVector(0.f, 0.f, 10.f));
	}
}

FText ADiningMessActor::GetCleanActionText() const
{
	return MessType == EDiningMessType::Puddle
		? NSLOCTEXT("DiningMess", "CleanPuddle", "清理积水")
		: NSLOCTEXT("DiningMess", "CleanTrash", "清理垃圾");
}

void ADiningMessActor::SetMessType(EDiningMessType NewType)
{
	if (MessType == NewType)
	{
		return;
	}
	MessType = NewType;
	ApplyTypeVisuals();
}

bool ADiningMessActor::TryClean(AShootCharacter* Player)
{
	if (bCleaned || !IsValid(Player))
	{
		return false;
	}
	bCleaned = true;

	if (Player->GetOverlappingMess() == this)
	{
		Player->SetOverlappingMess(nullptr);
	}

	if (bShowDebugInfo && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green,
			FString::Printf(TEXT("[就餐区] 已%s"), *GetCleanActionText().ToString()));
	}

	BP_OnCleaned(Player);
	OnCleaned.Broadcast(this);
	Destroy();
	return true;
}

void ADiningMessActor::OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		Character->SetOverlappingMess(this);
		if (bShowDebugInfo && Character->IsLocallyControlled() && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow,
				FString::Printf(TEXT("[就餐区] 按 E %s"), *GetCleanActionText().ToString()));
		}
	}
}

void ADiningMessActor::OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		if (Character->GetOverlappingMess() == this)
		{
			Character->SetOverlappingMess(nullptr);
		}
	}
}

void ADiningMessActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 兜底：Actor 被管理器清场 / 关卡卸载时清掉玩家身上的交互引用
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (AShootCharacter* Character = Cast<AShootCharacter>(PC->GetPawn()))
			{
				if (Character->GetOverlappingMess() == this)
				{
					Character->SetOverlappingMess(nullptr);
				}
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}
