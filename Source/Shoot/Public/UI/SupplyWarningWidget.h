// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "SupplyWarningWidget.generated.h"

class UBorder;
class UTextBlock;
class APlayerController;

/**
 * 生成箱低库存提醒（C++ 构建，无需 WBP）：
 * 某类食材的可用数量低于 UBaseGameInstance::SpawnerLowStockThreshold 时，
 * 在屏幕上持续显示“去野外采摘补充”的横幅；补足后自动隐藏。
 * 每个本地玩家一份实例（分屏下互不影响）。
 */
UCLASS()
class SHOOT_API USupplyWarningWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 为本地玩家创建（幂等）低库存提醒控件 */
	static void EnsureForPlayer(APlayerController* PC);

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void Refresh();

	UPROPERTY()
	UBorder* BannerBorder = nullptr;

	UPROPERTY()
	FSlateBrush BannerBrush;

	UPROPERTY()
	UTextBlock* TitleText = nullptr;

	UPROPERTY()
	UTextBlock* BodyText = nullptr;

	TWeakObjectPtr<APlayerController> OwnerController;

	float RefreshAccumulator = 0.f;
	bool bWarningVisible = false;
};
