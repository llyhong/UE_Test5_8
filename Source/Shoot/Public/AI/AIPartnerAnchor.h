// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/ShootAIPartnerController.h"
#include "AIPartnerAnchor.generated.h"

class UStaticMeshComponent;

/**
 * 关卡里摆放的 AI 搭档出生/驻守点。
 * 把它拖到后厨等位置即可：游戏开始在这里生成 AI 搭档，并把这里当作它的驻守点，
 * 空闲（非备料窗口 / 没有待取的锅）时搭档会自己走回来待命。
 */
UCLASS()
class SHOOT_API AAIPartnerAnchor : public AActor
{
	GENERATED_BODY()

public:
	AAIPartnerAnchor();

	/** 关卡开始时自动生成搭档；关掉则只作为白模摆件留在场景里 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|AIPartner")
	bool bSpawnOnBeginPlay = true;

	/** 初始职责：跟随 / 外勤采集 / 帮厨上菜（运行时仍可用 SetAIPartnerMode 切换） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|AIPartner")
	EAIPartnerMode InitialMode = EAIPartnerMode::Forager;

	/** 把锚点位置当作闲置驻守点（关掉则原地待命不动） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|AIPartner")
	bool bUseAsIdlePost = true;

	/** 编辑器里显示白模机器人方便摆位，游戏内自动隐藏（真正显示的是生成的搭档 pawn） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|AIPartner")
	bool bShowPlaceholder = true;

	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BodyComp;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> HeadComp;

	void ApplyPlaceholderVisibility();
};
