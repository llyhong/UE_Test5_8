// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/TutorialTaskManager.h"
#include "TutorialGuideWidget.generated.h"

class ATutorialTaskManager;
class APlayerController;
class UBorder;
class UVerticalBox;
class UTextBlock;
class UImage;
class UScrollBox;
class USizeBox;
class UTexture2D;

/**
 * 每日任务引导 HUD（C++ 构建，无需单独 WBP）：
 *  - 屏幕右侧任务清单（标题 + 每步完成状态与进度）；
 *  - 可见目标上方显示向下浮动的箭头，画面外目标使用方向引导；
 *  - 整局只保留一份实例，挂在 WBP_HUD 内；分屏时不复制给每个本地玩家。
 */
UCLASS()
class SHOOT_API UTutorialGuideWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UTutorialGuideWidget(const FObjectInitializer& ObjectInitializer);

	/** 兜底入口：HUD 尚未创建时（如自动化 UI 夹具）为主本地玩家创建唯一实例 */
	static void EnsureForPlayer(APlayerController* PC, ATutorialTaskManager* InManager);

	/** 由 HUD 调用：绑定任务管理器；已存在时只更新绑定，不重建清单 */
	void SetManager(ATutorialTaskManager* InManager);

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
	UTextBlock* HintText = nullptr;

	UPROPERTY()
	UVerticalBox* TaskColumn = nullptr;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> TaskLines;

	UPROPERTY()
	TArray<TObjectPtr<UBorder>> TaskRows;

	UPROPERTY()
	TArray<TObjectPtr<UImage>> TaskIcons;

	UPROPERTY()
	TObjectPtr<USizeBox> ChecklistSize;

	UPROPERTY()
	TObjectPtr<USizeBox> ScrollSize;

	UPROPERTY()
	TObjectPtr<UScrollBox> TaskScroll;

	UPROPERTY()
	TObjectPtr<UTexture2D> CompletionTexture;

	TArray<FTutorialChecklistEntry> CachedEntries;
	FText CachedDayTitle;
	float CachedChecklistWidth = 0.f;
	float CachedScrollHeight = 0.f;
	int32 ScrollTargetIndex = INDEX_NONE;

	/** 指向当前目标的导航箭头（icon_arrow_nav_right 贴图，绕中心旋转） */
	UPROPERTY()
	UImage* ArrowImage = nullptr;

	TWeakObjectPtr<APlayerController> OwnerController;

	/** 本次 Tick 计算出的箭头方向 / 是否可见 */
	FVector2D ArrowDir = FVector2D::ZeroVector;
	bool bArrowVisible = false;

	void RebuildChecklist(float Width);
};
