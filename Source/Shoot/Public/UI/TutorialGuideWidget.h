// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TutorialGuideWidget.generated.h"

class ATutorialTaskManager;
class APlayerController;
class UBorder;
class UVerticalBox;
class UTextBlock;
class UImage;

/**
 * 新手任务引导 HUD（C++ 构建，无需 WBP）：
 *  - 屏幕右侧任务清单（标题 + 每步完成状态与进度）；
 *  - 屏幕中央箭头指向当前目标（屏幕空间方向，按本地玩家独立创建，分屏互不影响）；
 *  - 只保留箭头，不再显示引导文案与距离。
 */
UCLASS()
class SHOOT_API UTutorialGuideWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 为本地玩家创建（幂等） */
	static void EnsureForPlayer(APlayerController* PC, ATutorialTaskManager* InManager);

	ATutorialTaskManager* GetManager() const { return Manager.Get(); }

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY()
	TObjectPtr<ATutorialTaskManager> Manager;

	UPROPERTY()
	UBorder* ChecklistBorder = nullptr;

	UPROPERTY()
	UTextBlock* TitleText = nullptr;

	UPROPERTY()
	UVerticalBox* TaskColumn = nullptr;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> TaskLines;

	/** 指向当前目标的导航箭头（icon_arrow_nav_right 贴图，绕中心旋转） */
	UPROPERTY()
	UImage* ArrowImage = nullptr;

	TWeakObjectPtr<APlayerController> OwnerController;

	/** 本次 Tick 计算出的箭头方向 / 是否可见 */
	FVector2D ArrowDir = FVector2D::ZeroVector;
	bool bArrowVisible = false;

	void RebuildChecklist();
};
