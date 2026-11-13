// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RecipeBookWidget.generated.h"

class APlayerController;
class UTextBlock;
class UScrollBox;
class USizeBox;
class UVerticalBox;
class UWidget;

struct FMenuData;

/**
 * 菜谱图鉴（C++ 构建，无需 WBP）：
 * - 已解锁菜品显示成品图标 / 菜名 / 所需食材 / 售价；
 * - 未解锁菜品显示剪影 + 菜品表的 ResearchHint 线索，让玩家知道"还有菜可研究、大概要什么"；
 * - 站在研究炉旁空手按 E 打开，再按 E / Esc 关闭。
 * 每个本地玩家一份实例，打开时切到 UI 输入模式（可滚动、可点关闭），关闭时还原游戏输入。
 */
UCLASS()
class SHOOT_API URecipeBookWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 为指定本地玩家开关图鉴：已打开则关闭；未打开则创建并刷新 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|UI")
	static void ToggleForPlayer(APlayerController* PC);

	/** 按当前解锁状态重建卡片列表（打开时调用；研究解锁后再打开即可看到新增菜谱） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|UI")
	void Refresh();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void NativeDestruct() override;

private:
	/** 单张菜品卡：bUnlocked=false 时只露剪影与线索 */
	UWidget* CreateCard(const FMenuData& Menu, int32 FoodID, bool bUnlocked);

	void CloseSelf();
	void SetUIInputEnabled(bool bEnabled);

	UPROPERTY()
	TObjectPtr<UTextBlock> SummaryText = nullptr;

	UPROPERTY()
	TObjectPtr<USizeBox> ScrollSize = nullptr;

	UPROPERTY()
	TObjectPtr<UScrollBox> ScrollBox = nullptr;

	/** 每行卡片数：按打开时的视口宽度决定，窄视口/分屏时自动减列，避免第三列被裁掉 */
	int32 Columns = 3;

	UPROPERTY()
	TObjectPtr<UVerticalBox> CardList = nullptr;

	UPROPERTY()
	TObjectPtr<UTextBlock> FooterText = nullptr;

	TWeakObjectPtr<APlayerController> OwnerController;
};
