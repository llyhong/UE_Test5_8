// Fill out your copyright notice in the Description page of Project Settings.

#include "Item/Dish.h"
#include "Game/PlateSystem.h"
#include "Kismet/GameplayStatics.h"

void ADish::OnRemoveActorEvent()
{
	FRemoveActorEvent.Broadcast();
	UWorld* World = GetWorld();
	if (World)
	{
		// 获取第一个找到的盘子系统 Actor
		APlateSystem* FoundActor = Cast<APlateSystem>(
			UGameplayStatics::GetActorOfClass(World, APlateSystem::StaticClass())
		);
		if (FoundActor)
		{
			FoundActor->RemovePlate();
		}
	}
}
