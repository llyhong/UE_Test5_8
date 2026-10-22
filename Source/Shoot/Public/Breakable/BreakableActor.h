// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces/HitInterface.h"
#include "BreakableActor.generated.h"

class UGeometryCollectionComponent;


UCLASS()
class SHOOT_API ABreakableActor : public AActor,public IHitInterface
{
	GENERATED_BODY()
	
public:	
	ABreakableActor();

protected:
	virtual void BeginPlay() override;

public:	
	virtual void Tick(float DeltaTime) override;
	//Blueprint Native Event NO.139
	virtual void GetHit_Implementation(const FVector& ImpactPoint, AActor* Hitter) override;
private:
	UPROPERTY(VisibleAnywhere)
	UGeometryCollectionComponent* GeometryCollection;
	
};
