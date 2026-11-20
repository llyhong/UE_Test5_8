#include "Item/PlateCollectionTray.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/BaseGameInstance.h"
#include "Game/EconomySystem.h"
#include "Machine/Dishwasher.h"
#include "UObject/ConstructorHelpers.h"

APlateCollectionTray::APlateCollectionTray()
{
	PrimaryActorTick.bCanEverTick = false;
	bPickable = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TrayMesh(
		TEXT("/Game/Fantastic_Village_Pack/meshes/props/deco/SM_PROP_plate_wood.SM_PROP_plate_wood"));
	if (TrayMesh.Succeeded())
	{
		ItemMesh->SetStaticMesh(TrayMesh.Object);
	}
}

void APlateCollectionTray::BeginPlay()
{
	Super::BeginPlay();
	RefreshCapacityFromUpgrades();
}

void APlateCollectionTray::RefreshCapacityFromUpgrades()
{
	const UBaseGameInstance* GI = GetWorld() ? Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()) : nullptr;
	const UEconomySystem* Eco = GI ? GI->GetEconomySystem() : nullptr;
	if (!Eco)
	{
		return;
	}

	int32 UpgradeCapacity = 0;
	if (Eco->GetOwnedItemCount(EconomyShopItemIDs::TrayLarge) > 0)
	{
		UpgradeCapacity = 8;
	}
	else if (Eco->GetOwnedItemCount(EconomyShopItemIDs::TrayMedium) > 0)
	{
		UpgradeCapacity = 6;
	}
	else if (Eco->GetOwnedItemCount(EconomyShopItemIDs::TraySmall) > 0)
	{
		UpgradeCapacity = 4;
	}

	// 只增不减：蓝图里配了更大的容量时以蓝图为下限
	Capacity = FMath::Max(Capacity, UpgradeCapacity);
}

int32 APlateCollectionTray::GetPlateCount() const
{
	int32 Count = 0;
	for (const AItemActor* Plate : Plates)
	{
		Count += IsValid(Plate) ? 1 : 0;
	}
	return Count;
}

void APlateCollectionTray::AddPlate(AItemActor* Plate)
{
	Plates.RemoveAll([](const AItemActor* Entry) { return !IsValid(Entry); });
	Plate->DeAttachMeshToSocket();
	Plate->AttachMeshToSocket(ItemMesh, NAME_None);
	const float Height = ItemMesh->GetStaticMesh() ? ItemMesh->GetStaticMesh()->GetBounds().GetBox().Max.Z : 0.f;
	const float PlateBottom = Plate->ItemMesh->GetStaticMesh()
		? Plate->ItemMesh->GetStaticMesh()->GetBounds().GetBox().Min.Z * Plate->ItemMesh->GetRelativeScale3D().Z : 0.f;
	Plate->ItemMesh->SetRelativeLocation(FVector(0.f, 0.f, Height - PlateBottom + 4.f * Plates.Num()));
	Plate->SetItemState(EItemState::EIS_Equipped);
	Plates.Add(Plate);
}

int32 APlateCollectionTray::WashPlates(ADishwasher& Washer)
{
	int32 Washed = 0;
	for (int32 Index = Plates.Num() - 1; Index >= 0; --Index)
	{
		AItemActor* Plate = Plates[Index];
		if (!IsValid(Plate))
		{
			Plates.RemoveAt(Index);
		}
		else if (Washer.SubmitPlate(Plate))
		{
			Plates.RemoveAt(Index);
			++Washed;
		}
	}
	return Washed;
}

void APlateCollectionTray::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		for (AItemActor* Plate : Plates)
		{
			if (IsValid(Plate))
			{
				Plate->Destroy();
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}
