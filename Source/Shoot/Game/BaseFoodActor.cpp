// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/BaseFoodActor.h"

// Sets default values
ABaseFoodActor::ABaseFoodActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMeshComponent"));

	// 成品（煎蛋/牛排这类摆在盘子上的模型）以前是完全无碰撞的：
	// 好处是不挡玩家，坏处是它和世界没有任何接触面 —— 摆到关卡里按 End 贴不到地、
	// 一旦所在的盘子被投掷/掉落，成品没有任何阻挡，直接穿地板掉出世界。
	// 这里只给"世界几何"开碰撞，Pawn 仍然 Ignore（落地的成品不会变成路障），
	// Visibility 也保持 Ignore（否则盘子上的一块煎蛋会挡住玩家看向桌上食物的交互射线）。
	ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ItemMesh->SetCollisionObjectType(ECC_WorldDynamic);
	ItemMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	ItemMesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	ItemMesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);

	RootComponent = ItemMesh;
}

// Called when the game starts or when spawned
void ABaseFoodActor::BeginPlay()
{
	Super::BeginPlay();

	SpawnWorldZ = GetActorLocation().Z;

	// 兜底：无论蓝图/父类里残留了什么，成品都不该自己受重力下坠。
	// 它要么挂在盘子/灶台上，要么独立静止；一旦模拟物理就会穿过没有 simple collision 的网格掉出世界。
	if (ItemMesh)
	{
		ItemMesh->SetSimulatePhysics(false);
		ItemMesh->SetEnableGravity(false);
	}

	UE_LOG(LogTemp, Verbose, TEXT("[FoodActor] %s BeginPlay Loc=%s Parent=%s Sim=%d Gravity=%d Collision=%d"),
		*GetName(),
		*GetActorLocation().ToCompactString(),
		*GetNameSafe(GetAttachParentActor()),
		ItemMesh ? (int32)ItemMesh->IsSimulatingPhysics() : -1,
		ItemMesh ? (int32)ItemMesh->IsGravityEnabled() : -1,
		ItemMesh ? static_cast<int32>(ItemMesh->GetCollisionEnabled()) : -1);
}

// Called every frame
void ABaseFoodActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 诊断：成品不该自己往下掉。一旦掉到生成高度以下，把当时的位置/父级/物理状态打出来一次，
	// 便于判断是"自己受重力"还是"被模拟物理的父物体带下去"。
	if (!bLoggedFall)
	{
		const float NowZ = GetActorLocation().Z;
		if (NowZ < SpawnWorldZ - 60.f)
		{
			bLoggedFall = true;
			UE_LOG(LogTemp, Error, TEXT("[FoodActor] %s 下坠 %.0fcm  Loc=%s Parent=%s Sim=%d Gravity=%d Collision=%d"),
				*GetName(),
				SpawnWorldZ - NowZ,
				*GetActorLocation().ToCompactString(),
				*GetNameSafe(GetAttachParentActor()),
				ItemMesh ? ItemMesh->IsSimulatingPhysics() : -1,
				ItemMesh ? ItemMesh->IsGravityEnabled() : -1,
				ItemMesh ? static_cast<int32>(ItemMesh->GetCollisionEnabled()) : -1);
		}
	}
}

void ABaseFoodActor::AttachMeshToSocket(USceneComponent* InParent, const FName& InSocketName)
{
	if (!InParent)
	{
		return;
	}

	// 盘子/灶台上如果没有配 PointSocket（目前 Content 里确实没有资产带这个 socket），
	// AttachToComponent 会退化成"挂到组件根 + 保持世界变换" → 成品留在生成时的世界原点，
	// 看起来就是煎蛋沉在地板下面。这里显式处理：socket 不存在就挂到父组件原点并归零相对变换。
	const bool bHasSocket = (InSocketName != NAME_None) && InParent->DoesSocketExist(InSocketName);

	const FAttachmentTransformRules TransformRules(EAttachmentRule::SnapToTarget, true);
	ItemMesh->AttachToComponent(InParent, TransformRules, bHasSocket ? InSocketName : NAME_None);

	if (!bHasSocket)
	{
		ItemMesh->SetRelativeTransform(FTransform::Identity);
	}
}