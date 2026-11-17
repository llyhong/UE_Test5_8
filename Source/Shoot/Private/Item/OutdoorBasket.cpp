// Fill out your copyright notice in the Description page of Project Settings.

#include "Item/OutdoorBasket.h"
#include "Data/ItemActorInfo.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AOutdoorBasket::AOutdoorBasket()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BasketMesh(TEXT("/Game/CropCharacters/Meshes/Tools/Basket.Basket"));
	if (BasketMesh.Succeeded() && ItemMesh)
	{
		ItemMesh->SetStaticMesh(BasketMesh.Object);
	}

	// 篮子作为“拿起就走”的容器，不进腐烂流程
	bEnableDecay = false;
}

void AOutdoorBasket::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	FitInteractionToMesh();
}

void AOutdoorBasket::BeginPlay()
{
	Super::BeginPlay();
	FitInteractionToMesh();
}

void AOutdoorBasket::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (ItemMesh && ItemMesh->IsSimulatingPhysics())
	{
		// 基类在物理翻滚时把球心锁回 Actor 正上方；篮子的枢轴在提手，须按网格中心重新对齐
		FitInteractionToMesh();
	}
}

void AOutdoorBasket::FitInteractionToMesh()
{
	if (!Sphere || !ItemMesh)
	{
		return;
	}
	const UStaticMesh* Mesh = ItemMesh->GetStaticMesh();
	if (!Mesh)
	{
		return;
	}

	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	// huanglily: Basket 网格枢轴在提手顶部（包围盒中心在原点下方约 28cm），
	// 基类按固定高度摆球会让判定球悬在篮子上方，站到篮子旁反而按不出来。
	const FVector MeshScale = ItemMesh->GetComponentScale();
	Sphere->SetRelativeLocation(Bounds.Origin);
	if (CollectSphere)
	{
		CollectSphere->SetRelativeLocation(Bounds.Origin);
	}

	const float MeshMaxScale = FMath::Max3(FMath::Abs(MeshScale.X), FMath::Abs(MeshScale.Y), FMath::Abs(MeshScale.Z));
	const float RequiredWorldRadius = Bounds.SphereRadius * MeshMaxScale + FMath::Max(0.f, InteractionRadiusPadding);
	auto EnsureRadiusCoversMesh = [RequiredWorldRadius](USphereComponent* Comp)
	{
		if (!Comp || Comp->GetScaledSphereRadius() >= RequiredWorldRadius)
		{
			return;
		}
		const FVector S = Comp->GetComponentScale();
		const float MaxScale = FMath::Max3(FMath::Abs(S.X), FMath::Abs(S.Y), FMath::Abs(S.Z));
		Comp->SetSphereRadius(RequiredWorldRadius / FMath::Max(MaxScale, KINDA_SMALL_NUMBER), false);
	};
	EnsureRadiusCoversMesh(Sphere);
	EnsureRadiusCoversMesh(CollectSphere);
}

float AOutdoorBasket::GetMeshBottomLocalZ() const
{
	if (!ItemMesh)
	{
		return 0.f;
	}
	const UStaticMesh* Mesh = ItemMesh->GetStaticMesh();
	if (!Mesh)
	{
		return 0.f;
	}
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	return (Bounds.Origin.Z - Bounds.BoxExtent.Z) * ItemMesh->GetRelativeScale3D().Z;
}

void AOutdoorBasket::AttachMeshToSocket(USceneComponent* InParent, const FName& InSocketName)
{
	FName SocketName = InSocketName;
	if (SocketName.IsNone())
	{
		SocketName = HeldSocketName;
	}
	if (InParent && !SocketName.IsNone() && !InParent->DoesSocketExist(SocketName))
	{
		// 角色没有配手部插槽时退回手骨插槽，保证篮子像挂饰一样跟着手走
		SocketName = InParent->DoesSocketExist(HeldSocketName) ? HeldSocketName : NAME_None;
	}

	// 角色骨架（RetargetMesh）通常带整体缩放，直接 SnapToTarget 会把篮子一起放大
	const bool bHangOnCharacter = InParent && InParent->IsA<USkeletalMeshComponent>()
		&& Cast<AShootCharacter>(InParent->GetOwner());
	if (bHangOnCharacter)
	{
		CachedWorldScaleBeforeAttach = GetActorScale3D();
		bScaleCompensated = true;
	}

	Super::AttachMeshToSocket(InParent, SocketName);

	// 只在挂到角色骨架时应用微调偏移；摆到灶台/仓库等其它挂点时保持插槽原样
	if (bHangOnCharacter)
	{
		// huanglily: 角色骨架把手插槽的骨骼缩放到 2.25，组件本身仍是 1；
		// 必须按插槽世界缩放反算，否则篮子跟着骨骼一起被撑大。
		const FVector SocketScale = SocketName.IsNone()
			? InParent->GetComponentScale()
			: InParent->GetSocketTransform(SocketName, ERelativeTransformSpace::RTS_World).GetScale3D();
		auto SafeScale = [](float S) { return FMath::Max(FMath::Abs(S), KINDA_SMALL_NUMBER); };
		const FVector TargetWorldScale = CachedWorldScaleBeforeAttach * FMath::Max(0.05f, HeldMeshScale);
		ItemMesh->SetRelativeScale3D(FVector(
			TargetWorldScale.X / SafeScale(SocketScale.X),
			TargetWorldScale.Y / SafeScale(SocketScale.Y),
			TargetWorldScale.Z / SafeScale(SocketScale.Z)));
		// 偏移也按插槽缩放反算，调参时看到的世界距离才直观
		ItemMesh->SetRelativeLocation(FVector(
			HeldMeshOffset.X / SafeScale(SocketScale.X),
			HeldMeshOffset.Y / SafeScale(SocketScale.Y),
			HeldMeshOffset.Z / SafeScale(SocketScale.Z)));
		ItemMesh->SetRelativeRotation(HeldMeshRotation);
	}
}

void AOutdoorBasket::DeAttachMeshToSocket()
{
	Super::DeAttachMeshToSocket();
	if (bScaleCompensated)
	{
		// Detach 会保留世界变换（含骨架放大的那部分），脱手后还原成关卡里的摆放尺寸
		ItemMesh->SetWorldScale3D(CachedWorldScaleBeforeAttach);
		bScaleCompensated = false;
	}
}

int32 AOutdoorBasket::GetUsedCount() const
{
	int32 Total = 0;
	for (const FInventorySlot& Slot : BasketItems)
	{
		Total += Slot.Quantity;
	}
	return Total;
}

int32 AOutdoorBasket::GetRemainingSpace() const
{
	return FMath::Max(0, BasketCapacity - GetUsedCount());
}

bool AOutdoorBasket::TryAddItem(AItemActor* Item, int32 Quantity)
{
	if (!IsValid(Item) || Quantity <= 0 || GetRemainingSpace() < Quantity)
	{
		return false;
	}

	const int32 ExistingIndex = BasketItems.IndexOfByPredicate([Item](const FInventorySlot& Slot)
	{
		return Slot.ItemClass == Item->GetClass();
	});

	if (ExistingIndex != INDEX_NONE)
	{
		BasketItems[ExistingIndex].Quantity += Quantity;
	}
	else
	{
		FInventorySlot Slot;
		Slot.ItemClass = Item->GetClass();
		Slot.ItemInfoRow = Item->ItemInfoRow;
		Slot.FoodIdArray = Item->FoodIdArray;
		if (const FItemActorInfo* InfoRow = Item->ItemInfoRow.GetRow<FItemActorInfo>(TEXT("OutdoorBasket")))
		{
			Slot.DisplayName = InfoRow->ItemDes;
		}
		Slot.Quantity = Quantity;
		BasketItems.Add(Slot);
	}

	// 入篮的只是数据，世界上的果实/食材载体立即回收
	Item->Destroy();
	OnBasketChanged.Broadcast();
	return true;
}

void AOutdoorBasket::SetBasketCapacity(int32 NewCapacity)
{
	BasketCapacity = FMath::Max(1, NewCapacity);
	OnBasketChanged.Broadcast();
}

int32 AOutdoorBasket::UpgradeCapacity(int32 Extra)
{
	if (Extra > 0)
	{
		BasketCapacity = FMath::Clamp(BasketCapacity + Extra, 1, 999);
		OnBasketChanged.Broadcast();
	}
	return BasketCapacity;
}

void AOutdoorBasket::EmptyBasket()
{
	if (BasketItems.Num() > 0)
	{
		BasketItems.Empty();
		OnBasketChanged.Broadcast();
	}
}

void AOutdoorBasket::PlaceBesideWarehouse(AActor* Warehouse, const FVector& PlayerFeetLocation)
{
	if (!Warehouse)
	{
		SetActorLocation(PlayerFeetLocation);
		return;
	}

	// 放在仓库朝向玩家的一侧，贴着包围盒边缘，玩家放完转身就能再拿
	const FBox Box = Warehouse->GetComponentsBoundingBox(true);
	FVector Center = Warehouse->GetActorLocation();
	FVector Extent(60.f, 60.f, 0.f);
	FVector Min = Center;
	if (Box.IsValid)
	{
		Center = Box.GetCenter();
		Extent = Box.GetExtent();
		Min = Box.Min;
	}

	FVector Dir = PlayerFeetLocation - Center;
	Dir.Z = 0.f;
	if (!Dir.Normalize())
	{
		Dir = Warehouse->GetActorForwardVector().GetSafeNormal2D();
	}
	// 投影到包围盒上的距离近似：按方向取各轴半长，再留出一点空隙
	const float Reach = FMath::Abs(Dir.X) * Extent.X + FMath::Abs(Dir.Y) * Extent.Y;
	FVector Target = Center + Dir * (Reach + 70.f);
	// 枢轴在提手顶部：按网格最低点反算，否则半截篮子会埋进地面
	Target.Z = FMath::Max(PlayerFeetLocation.Z, Min.Z) + 2.f - GetMeshBottomLocalZ();
	SetActorLocation(Target);

	// 篮子只做静态摆放：不模拟物理，否则贴着墙/地板时会被弹飞
	if (ItemMesh)
	{
		ItemMesh->SetSimulatePhysics(false);
		ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetItemState(EItemState::EIS_Idle);
}
