// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "LevelActor.generated.h"

class UWidgetComponent;

UCLASS()
class SHOOT_API ALevelActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ALevelActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	/** Widget 组件 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWidgetComponent> WidgetComp;

	/** 关联的 Data Table Row */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FDataTableRowHandle LevelDataTableRow;

	/** 光标悬停进入时触发（蓝图可重写） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Cursor")
	void OnBeginCursorOverlap();

	/** 光标悬停离开时触发（蓝图可重写） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Cursor")
	void OnEndCursorOverlap();

	/** 显示 Widget */
	void ShowWidget();

	/** 隐藏 Widget */
	void HideWidget();
};
