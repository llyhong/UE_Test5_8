// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/OrderWidget.h"
#include "OrderHUDWidget.generated.h"

class UCanvasPanel;
class UHorizontalBox;
class UImage;
class UProgressBar;
class USizeBox;
class UTextBlock;
class USoundBase;
class UTexture2D;

/**
 * 新版订单 HUD：顶部居中、横向排列的订单卡片条。
 *
 * 与旧版 UI_TopOrderTips / UI_OrderTips 并存：旧资源不改动，本类是可独立挂载的新实现。
 * 布局在运行时用 UMG 组件搭建（不依赖 WBP 设计器里的控件树），因此美术参数集中在 cpp 顶部常量里。
 *
 * 每张卡片显示：
 *   - 成品图标（菜品表 MenuIcon）
 *   - 菜名（Handle->GetOrderDisplayName）
 *   - 金币 + 价格（Handle->GetOrderPrice）
 *   - 食材图标（Handle->GetOrderFoodIcons，最多 4 个，超出显示 +N）
 *   - 时钟 + 等待进度条（Handle->GetPercent，每帧刷新）
 *
 * 数据同步走 WaitingOrders（由 UOrderWidget 基类维护），本类在 Tick 中比对句柄列表做增量
 * 增删卡片，不改动基类的蓝图事件，因此旧版订单 UI 的行为完全不受影响。
 */
UCLASS()
class SHOOT_API UOrderHUDWidget : public UOrderWidget
{
	GENERATED_BODY()

public:
	UOrderHUDWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void HandleOnTimeStreakChanged(int32 Count) override;

	/** 单张卡片上需要动态改写的控件 */
	struct FCardWidgets
	{
		TObjectPtr<UOrderHandle> Handle = nullptr;
		TObjectPtr<USizeBox> Root = nullptr;
		TObjectPtr<UImage> DishIcon = nullptr;
		TObjectPtr<UTextBlock> NameText = nullptr;
		TObjectPtr<UTextBlock> PriceText = nullptr;
		TObjectPtr<UTextBlock> MoreText = nullptr;
		TArray<TObjectPtr<UImage>> IngredientIcons;
		TArray<TObjectPtr<class UBorder>> IngredientTiles;
		TObjectPtr<UImage> ClockIcon = nullptr;
		TObjectPtr<UProgressBar> WaitBar = nullptr;
		float SpawnTime = 0.f;
	};

	/** 首次挂载时搭建根画布与订单行 */
	void BuildLayout();

	/** 对比 WaitingOrders 与当前卡片；有增删/换序时重建 */
	void SyncCards();

	/** 按 WaitingOrders 重建全部卡片（已存在句柄保留出场时间，避免整排重播动画） */
	void RebuildCards();

	/** 创建一张卡片并挂到订单行上 */
	FCardWidgets CreateCard(UOrderHandle* Handle);

	/** 把订单数据写进卡片控件（名称/价格/图标/食材） */
	void FillCard(FCardWidgets& Card);

	/** 取本单成品图标：优先菜品表 MenuIcon，取不到退回第一个食材图标 */
	UTexture2D* ResolveDishIcon(UOrderHandle* Handle) const;

private:
	UPROPERTY()
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY()
	TObjectPtr<UHorizontalBox> OrderRow;

	UPROPERTY()
	TObjectPtr<class UBorder> StreakBadge;

	UPROPERTY()
	TObjectPtr<UTextBlock> StreakText;

	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Order")
	TObjectPtr<USoundBase> StreakChime;

	float StreakTimeRemaining = 0.f;

	UPROPERTY()
	TObjectPtr<UTexture2D> CoinTexture;

	UPROPERTY()
	TObjectPtr<UTexture2D> ClockTexture;

	/** 当前显示的卡片（控件本身由 WidgetTree/父面板持有，这里只做数据索引） */
	TArray<FCardWidgets> Cards;

	bool bLayoutBuilt = false;
};
