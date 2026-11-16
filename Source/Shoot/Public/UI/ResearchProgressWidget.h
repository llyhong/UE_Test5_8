// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ResearchProgressWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UBorder;
class UHorizontalBox;
class USizeBox;
class UWidget;

/**
 * 设备头顶的世界空间进度卡片（C++ 构建，无需 WBP），研究炉与烹饪锅共用。
 * 由上层的 WidgetComponent 创建，炉内有食材或设备工作期间可见：
 * 标题（"研究中"/"烹饪中"/备料提示）+ 剩余秒数、投入食材的图标行、进度条。
 */
UCLASS()
class SHOOT_API UResearchProgressWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Remaining/Total 单位为秒；Total<=0 时按 0% 显示。InVerb 为标题动词 */
	void SetProgress(float Remaining, float Total, const FString& InVerb = TEXT("研究中"));

	/** 备料阶段（还没开始计时）：显示"已投入 N 份 · InHint"，进度条归零 */
	void SetPending(int32 IngredientCount, const FString& InHint = TEXT("凑齐后开始研究"));

	/** 食材图标行：按投入顺序显示（同一食材重复时合并成 ×N） */
	void SetIngredientList(const TArray<int32>& ItemIDs);

	/** 食材图标行：食材ID→数量（烹饪锅直接传 FoodMaterial） */
	void SetIngredientCounts(const TMap<int32, int32>& Counts);

	/** 当前卡片应有尺寸：无图标 220×48，带图标行 220×74。
	 *  世界空间控件不会自动长高，调用方需同步 WidgetComponent->SetDrawSize() */
	FVector2D GetCardDrawSize() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void ApplyIngredientEntries(const TArray<TPair<int32, int32>>& Entries);
	UWidget* MakeIconSlot(int32 ItemID, int32 Count);

	UPROPERTY()
	TObjectPtr<USizeBox> Root = nullptr;

	UPROPERTY()
	TObjectPtr<UBorder> CardBorder = nullptr;

	UPROPERTY()
	TObjectPtr<UTextBlock> Label = nullptr;

	UPROPERTY()
	TObjectPtr<UHorizontalBox> IconRow = nullptr;

	UPROPERTY()
	TObjectPtr<UProgressBar> Bar = nullptr;

	FString Verb = TEXT("研究中");

	/** 已应用的图标行内容签名，避免每帧重建 UMG 子控件 */
	FString IconSignature;

	bool bHasIcons = false;
};
