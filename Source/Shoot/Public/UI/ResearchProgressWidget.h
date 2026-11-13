// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ResearchProgressWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UBorder;

/**
 * 研究炉头顶的世界空间进度条（C++ 构建，无需 WBP）。
 * 由 AResearchStove 的 ProgressWidgetComp 创建，仅研究期间可见：
 * 进度条 + "研究中 · 剩余秒数" 文本，避免玩家只能盯着屏幕角落的调试消息。
 */
UCLASS()
class SHOOT_API UResearchProgressWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Remaining/Total 单位为秒；Total<=0 时按 0% 显示 */
	void SetProgress(float Remaining, float Total);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY()
	TObjectPtr<UBorder> CardBorder = nullptr;

	UPROPERTY()
	TObjectPtr<UProgressBar> Bar = nullptr;

	UPROPERTY()
	TObjectPtr<UTextBlock> Label = nullptr;
};
