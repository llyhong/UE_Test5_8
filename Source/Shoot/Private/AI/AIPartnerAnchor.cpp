// Fill out your copyright notice in the Description page of Project Settings.

#include "AI/AIPartnerAnchor.h"

#include "Components/StaticMeshComponent.h"
#include "Game/BaseGameInstance.h"
#include "UObject/ConstructorHelpers.h"

AAIPartnerAnchor::AAIPartnerAnchor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	// huanglily：必须有 Root，摆放后 Transform 才会被关卡保存/移动生效
	RootComponent = SceneRoot;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));

	BodyComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	BodyComp->SetupAttachment(SceneRoot);
	BodyComp->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	BodyComp->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.2f));
	BodyComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CubeFinder.Succeeded())
	{
		BodyComp->SetStaticMesh(CubeFinder.Object);
	}

	HeadComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	HeadComp->SetupAttachment(SceneRoot);
	HeadComp->SetRelativeLocation(FVector(0.f, 0.f, 140.f));
	HeadComp->SetRelativeScale3D(FVector(0.45f, 0.45f, 0.45f));
	HeadComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CubeFinder.Succeeded())
	{
		HeadComp->SetStaticMesh(CubeFinder.Object);
	}
}

void AAIPartnerAnchor::BeginPlay()
{
	Super::BeginPlay();

	ApplyPlaceholderVisibility();

	if (!bSpawnOnBeginPlay)
	{
		return;
	}

	UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	if (!GI)
	{
		return;
	}

	// huanglily：关卡里可能摆了多个锚点，只认第一个，避免生成出重复搭档
	if (GI->HasAIPartner())
	{
		return;
	}

	const FVector IdlePost = bUseAsIdlePost ? GetActorLocation() : FVector::ZeroVector;
	// huanglily：锚点贴地摆放（方便摆位/白模不悬空），生成 pawn 时抬高一个胶囊，避免出生就卡进地板
	FTransform SpawnTF = GetActorTransform();
	SpawnTF.AddToTranslation(FVector(0.f, 0.f, 100.f));
	if (GI->SpawnAIPartnerAt(SpawnTF, IdlePost, InitialMode))
	{
		UE_LOG(LogTemp, Log, TEXT("[AIPartner] 锚点 %s 已生成搭档，驻守点 %s"), *GetName(), *IdlePost.ToString());
	}
}

void AAIPartnerAnchor::ApplyPlaceholderVisibility()
{
	// huanglily：白模只是编辑器摆位参考，进游戏后由真正的搭档 pawn 站同一个位置
	const bool bVisibleInEditor = bShowPlaceholder;
	if (BodyComp)
	{
		BodyComp->SetVisibility(bVisibleInEditor, true);
		BodyComp->SetHiddenInGame(true);
	}
	if (HeadComp)
	{
		HeadComp->SetVisibility(bVisibleInEditor, true);
		HeadComp->SetHiddenInGame(true);
	}
}
