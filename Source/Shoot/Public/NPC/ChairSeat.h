// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChairSeat.generated.h"

class UStaticMeshComponent;
class AGuestCharacter;
class AItemActor;

/** 座位被“空碟”回收而释放时广播（由客人管理器监听以安排下一位客人入座） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSeatFreedSignature);

/**
 * 独立的凳子类，表示客人可坐下的座位。
 * 提供座位插槽位置（SitSocket）与占用状态管理，避免多个客人抢占同一座位。
 * 方案B：客人吃完离座后，空碟会临时占据座位（bHasDirtyPlate），直到玩家按 E 回收空碟才真正释放。
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

	/** 释放座位（清空占用者、空碟标记，但不广播委托） */
	UFUNCTION(BlueprintCallable, Category = "Chair")
	void Release();

	/** 客人离座后，由空碟临时占据本座位（bIsOccupied 仍为真，直到空碟被回收） */
	UFUNCTION(BlueprintCallable, Category = "Chair")
	void SetDirtyPlate(AItemActor* Plate);

	/** 回收空碟时调用：真正释放座位并广播 OnSeatFreedSignature */
	UFUNCTION(BlueprintCallable, Category = "Chair")
	void ClearDirtyPlate();

	/** 返回坐下时的世界坐标与朝向 */
	UFUNCTION(BlueprintCallable, Category = "Chair")
	FTransform GetSitTransform() const;

	/** 当前占据本座位的空碟（无则为 nullptr）。仅空碟模式下有效 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair")
	TObjectPtr<AItemActor> DirtyPlate = nullptr;

	/** 是否被空碟占用（客人已离座，但空碟尚未被回收） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chair")
	bool bHasDirtyPlate = false;

	/** 座位被“空碟回收”释放时广播（客人管理器监听以安排下一位客人入座） */
	UPROPERTY(BlueprintAssignable, Category = "Chair")
	FOnSeatFreedSignature OnSeatFreedDelegate;

	/** 是否绘制调试信息：从本座位到当前占用客人(Occupant)画一条连线，并在两端做标记，方便排查占座/卡座 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chair|Debug")
	bool bDrawDebugSeatLink = false;

	virtual void Tick(float DeltaTime) override;
};
