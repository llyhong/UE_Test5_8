// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "Data/DiscoveryInfo.h"
#include "DiscoveryToastWidget.generated.h"

class UBorder;
class UImage;
class USizeBox;
class UTextBlock;
class UHorizontalBoxSlot;
class APlayerController;

/** 单条弹窗队列项：首次发现 / 普通提示共用同一套卡片 */
USTRUCT()
struct FDiscoveryToastEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FDiscoveryInfo Info;

	UPROPERTY()
	FLinearColor Accent = FLinearColor(0.95f, 0.78f, 0.35f, 1.f);

	/** true=首次发现卡片（显示“新发现！”与奖励行）；false=普通提示（只显示标题/正文） */
	UPROPERTY()
	bool bDiscovery = true;
};

/**
 * 轻量提示卡片（C++ 构建，无需 WBP）：
 * - ShowForPlayer：首次发现弹窗（图标 / 名称 / 描述 / 奖励）
 * - ShowMessage：通用提示（如“浇水时机正好”“照料连击”）
 * 每个本地玩家一份实例，队列播放，避免多人同屏共享控件。
 */
UCLASS()
class SHOOT_API UDiscoveryToastWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 向指定本地玩家弹出一条“首次发现”卡片 */
	static void ShowForPlayer(APlayerController* PC, const FDiscoveryInfo& Info);

	/** 向指定本地玩家弹出一条普通提示卡片 */
	static void ShowMessage(APlayerController* PC, const FString& Title, const FString& Body, FLinearColor Accent);

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void Enqueue(const FDiscoveryToastEntry& Entry);
	void BeginNext();
	void ApplyVisual(const FDiscoveryToastEntry& Entry);

	UPROPERTY()
	TArray<FDiscoveryToastEntry> Queue;

	UPROPERTY()
	UBorder* CardBorder = nullptr;

	/** 卡片底刷（描边颜色随提示主色改，需保留一份可改的副本） */
	UPROPERTY()
	FSlateBrush CardBrush;

	UPROPERTY()
	USizeBox* IconBox = nullptr;

	UPROPERTY()
	UImage* IconImage = nullptr;

	UPROPERTY()
	UHorizontalBoxSlot* IconSlot = nullptr;

	UPROPERTY()
	UTextBlock* KickerText = nullptr;

	UPROPERTY()
	UTextBlock* NameText = nullptr;

	UPROPERTY()
	UTextBlock* DescText = nullptr;

	UPROPERTY()
	UTextBlock* RewardText = nullptr;

	TWeakObjectPtr<APlayerController> OwnerController;

	bool bShowing = false;
	float Elapsed = 0.f;
};
