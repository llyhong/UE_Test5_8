#include "Item/HarvestBasketArea.h"

#include "Components/BoxComponent.h"
#include "Game/BaseGameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Item/OutdoorBasket.h"
#include "Machine/StorageWarehouse.h"
#include "ShootCharacter.h"

AHarvestBasketArea::AHarvestBasketArea()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
	KitchenBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("KitchenBounds"));
	SetRootComponent(KitchenBounds);
	KitchenBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	KitchenBounds->SetGenerateOverlapEvents(false);
}

void AHarvestBasketArea::BeginPlay()
{
	Super::BeginPlay();
	if (!IsValid(Basket))
	{
		SetActorTickEnabled(false);
		return;
	}
	DockTransform = Basket->GetActorTransform();
}

bool AHarvestBasketArea::IsInside(const FVector& Location, double Margin) const
{
	const FVector Local = KitchenBounds->GetComponentTransform().InverseTransformPosition(Location).GetAbs();
	const FVector Extent = KitchenBounds->GetUnscaledBoxExtent();
	return Local.X < Extent.X + Margin && Local.Y < Extent.Y + Margin && Local.Z < Extent.Z;
}

bool AHarvestBasketArea::IsBasketAvailableInKitchen() const
{
	return IsValid(Basket) && Basket->bPickable && !Basket->IsActorBeingDestroyed()
		&& !Basket->GetAttachParentActor() && Basket->GetItemState() != EItemState::EIS_Equipped
		&& IsInsideKitchen(Basket->GetActorLocation());
}

void AHarvestBasketArea::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsValid(Basket))
	{
		return;
	}

	const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance());
	if (GI && !GI->bRoleSelectedThisSession)
	{
		return;
	}

	for (auto It = Crossings.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) It.RemoveCurrent();
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (AShootCharacter* Player = PC && PC->IsLocalController() ? Cast<AShootCharacter>(PC->GetPawn()) : nullptr)
		{
			UpdatePlayer(*Player);
		}
	}
}

void AHarvestBasketArea::UpdatePlayer(AShootCharacter& Player)
{
	constexpr double BoundaryBuffer = 28.0;
	const FVector Location = Player.GetActorLocation();
	const TWeakObjectPtr<AShootCharacter> Key(&Player);
	FPlayerCrossing* Crossing = Crossings.Find(Key);
	if (!Crossing)
	{
		const bool bInside = IsInside(Location, 0.0);
		// Restore saves that put a player holding the basket back inside the kitchen.
		Crossings.Add(Key, {bInside, bInside && Player.GetEquippedBasket() == Basket});
		return;
	}

	const bool bInside = IsInside(Location, Crossing->bInside ? BoundaryBuffer : -BoundaryBuffer);
	if (bInside != Crossing->bInside)
	{
		Crossing->bInside = bInside;
		Crossing->bPending = true;
	}
	if (!Crossing->bPending || !Player.CanSwitchHarvestBasket())
	{
		return;
	}
	Crossing->bPending = false;

	const FLinearColor Accent(0.66f, 0.84f, 0.35f);
	if (bInside)
	{
		if (Player.GetEquippedBasket() != Basket)
		{
			return;
		}
		const int32 Moved = IsValid(Warehouse) ? Warehouse->DepositBasket(Basket) : 0;
		Player.ReturnHarvestBasket(DockTransform);
		if (Basket->GetUsedCount() > 0)
		{
			Player.ShowSimpleToast(TEXT("篮子已归位"), TEXT("未入库的食材留在篮里"), Accent);
		}
		else if (Moved > 0)
		{
			Player.ShowSimpleToast(TEXT("篮子已归位"), FString::Printf(TEXT("已将 %d 份食材存入冰箱"), Moved), Accent);
		}
		else
		{
			Player.ShowSimpleToast(TEXT("篮子已归位"), TEXT("下次出门会自动带上"), Accent);
		}
		return;
	}

	if (!Player.GetPickingItem() && IsBasketAvailableInKitchen())
	{
		Player.HoldItemInHand(Basket);
		Player.ShowSimpleToast(TEXT("已带上采集篮"), TEXT("按 E 采摘或把食材装进篮子"), Accent);
	}
}
