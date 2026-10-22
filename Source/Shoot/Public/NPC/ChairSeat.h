// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChairSeat.generated.h"

class UStaticMeshComponent;
class AGuestCharacter;

/**
 * 独立的凳子类，表示客人可坐下的座位。
 * 提供座位插槽位置（SitSocket）与占用状态管理，避免多个客人抢占同一座位。
 */
UCLASS()
class SHOOT_API AChairSeat : public AActor
{
	GENERATED_BODY()

public:
	AChairSeat();

	/** 凳子网格体 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair")
	TObjectPtr<UStaticMeshComponent> SeatMesh;

	/** 坐下插槽：相对凳子的偏移（默认在凳子上方 50cm 处），可在蓝图微调 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chair")
	FVector SitSocketOffset = FVector(0.f, 0.f, 50.f);

	/** 该座位是否被占用 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair")
	bool bIsOccupied = false;

	/** 当前占用该座位的客人 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair")
	TObjectPtr<AGuestCharacter> Occupant;

	/** 占用座位（成功返回 true；已被占用返回 false） */
	UFUNCTION(BlueprintCallable, Category = "Chair")
	bool Occupy(AGuestCharacter* Guest);

	/** 释放座位 */
	UFUNCTION(BlueprintCallable, Category = "Chair")
	void Release();

	/** 返回坐下时的世界坐标与朝向 */
	UFUNCTION(BlueprintCallable, Category = "Chair")
	FTransform GetSitTransform() const;

	/** 是否绘制调试信息：从本座位到当前占用客人(Occupant)画一条连线，并在两端做标记，方便排查占座/卡座 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chair|Debug")
	bool bDrawDebugSeatLink = false;

	virtual void Tick(float DeltaTime) override;
};
