// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BaseFoodActor.generated.h"

UCLASS()
class SHOOT_API ABaseFoodActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ABaseFoodActor();
	UFUNCTION(BlueprintCallable, Category="Food Actor")
	void AttachMeshToSocket(USceneComponent* InParent, const FName& InSocketName);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UStaticMeshComponent* ItemMesh;
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

private:
	/** 生成时的世界高度：用来判断"是不是掉下去了" */
	float SpawnWorldZ = 0.f;
	/** 掉落诊断只打一次，避免刷屏 */
	bool bLoggedFall = false;
};
