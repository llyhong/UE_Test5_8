// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/ChairSeat.h"
#include "Components/StaticMeshComponent.h"
#include "NPC/GuestCharacter.h"
#include "DrawDebugHelpers.h"

AChairSeat::AChairSeat()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SeatMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SeatMesh"));
	SeatMesh->SetupAttachment(RootComponent);
}

bool AChairSeat::Occupy(AGuestCharacter* Guest)
{
	if (bIsOccupied || !Guest)
	{
		return false;
	}
	bIsOccupied = true;
	Occupant = Guest;
	return true;
}

void AChairSeat::Release()
{
	bIsOccupied = false;
	Occupant = nullptr;
	DirtyPlate = nullptr;
	bHasDirtyPlate = false;
}

void AChairSeat::SetDirtyPlate(AItemActor* Plate)
{
	// 客人已离座但空碟暂占座位：保持 bIsOccupied 为真，避免新客人抢占，
	// 直到玩家按 E 回收空碟后 ClearDirtyPlate 才真正释放。
	bIsOccupied = true;
	Occupant = nullptr;
	DirtyPlate = Plate;
	bHasDirtyPlate = true;
}

void AChairSeat::ClearDirtyPlate()
{
	bIsOccupied = false;
	Occupant = nullptr;
	DirtyPlate = nullptr;
	bHasDirtyPlate = false;
	// 真正释放座位：通知客人管理器安排下一位客人入座
	OnSeatFreedDelegate.Broadcast();
}

FTransform AChairSeat::GetSitTransform() const
{
	const FVector SitLocation = GetActorLocation() + SitSocketOffset;
	const FRotator SitRotation = GetActorRotation();
	return FTransform(SitRotation, SitLocation, FVector::OneVector);
}

void AChairSeat::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 调试：从座位到当前占用客人画一条连线，并在两端做标记。
	// 仅当开启开关且存在有效占用客人时绘制，便于排查“占座但无客人”/“卡在桌边”等问题。
	if (!bDrawDebugSeatLink)
	{
		return;
	}

	const FVector SeatLoc = GetActorLocation() + SitSocketOffset;
	DrawDebugSphere(GetWorld(), SeatLoc, 12.f, 8, FColor::Green, false, -1.f, 0, 2.f);

	if (IsValid(Occupant))
	{
		const FVector GuestLoc = Occupant->GetActorLocation();
		// 座位 -> 占用客人 的连线
		DrawDebugLine(GetWorld(), SeatLoc, GuestLoc,
			bIsOccupied ? FColor::Yellow : FColor::Red, false, -1.f, 0, 2.f);
		// 占用客人位置标记
		DrawDebugSphere(GetWorld(), GuestLoc, 15.f, 8, FColor::Yellow, false, -1.f, 0, 2.f);

		// if (GEngine)
		// {
		// 	GEngine->AddOnScreenDebugMessage(-1, 0.1f, FColor::Yellow,
		// 		FString::Printf(TEXT("[ChairSeat] %s -> Occupant %s 距离: %.1f"),
		// 			*GetName(), *Occupant->GetName(),
		// 			FVector::Dist(SeatLoc, GuestLoc)));
		// }
	}
	else if (bIsOccupied)
	{
		// 被标记占用但客人已失效：用红色标记座位，提示“幽灵占用”
		DrawDebugSphere(GetWorld(), SeatLoc, 16.f, 8, FColor::Red, false, -1.f, 0, 3.f);
	}
}
