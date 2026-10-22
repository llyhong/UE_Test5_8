// Fill out your copyright notice in the Description page of Project Settings.


#include "Interfaces/HoveringInterface.h"

// Add default functionality here for any IHoveringInterface functions that are not pure virtual.
void IHoveringInterface::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
}

void IHoveringInterface::OnSphereEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
}
