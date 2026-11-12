// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "SeasonWarningWidget.generated.h"

class UBorder;
class UTextBlock;
class APlayerController;

/** 单条警告队列项（同一天可能同时触发多条失败线） */
USTRUCT()
struct FSeasonWarningEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString Reason;

	UPROPERTY()
	int32 StreakDays = 0;

	UPROPERTY()
	int32 DaysLeft = 0;
};

/**
 * 赛季警告条（C++ 构建，无需 WBP）：屏幕顶部居中的红色横条。
 * 连续计数线触发时弹出，队列播放，每条约 3.5 秒。
 */
UCLASS()
class SHOOT_API USeasonWarningWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static void ShowForPlayer(APlayerController* PC, const FString& Reason, int32 StreakDays, int32 DaysLeft);

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void Enqueue(const FSeasonWarningEntry& Entry);
	void BeginNext();
	void Apply(const FSeasonWarningEntry& Entry);

	UPROPERTY()
	TArray<FSeasonWarningEntry> Queue;

	UPROPERTY()
	UBorder* BarBorder = nullptr;

	UPROPERTY()
	FSlateBrush BarBrush;

	UPROPERTY()
	UTextBlock* ReasonText = nullptr;

	UPROPERTY()
	UTextBlock* DetailText = nullptr;

	TWeakObjectPtr<APlayerController> OwnerController;

	bool bShowing = false;
	float Elapsed = 0.f;
};
