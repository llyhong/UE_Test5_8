// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SpawnerRestockTipWidget.generated.h"

class UTextBlock;

/**
 * 食材箱低库存提示（C++ 构建，无需 WBP）：
 * 显示在箱子正上方，提示玩家该食材不足、需要去野外补充。
 * 显隐由 AFoodSpawner 按“库存 + 玩家距离”控制。
 */
UCLASS()
class SHOOT_API USpawnerRestockTipWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 更新提示文案（标题含食材名与库存，正文为补货指引） */
	void SetTip(const FText& InTitle, const FText& InBody);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY()
	UTextBlock* TitleText = nullptr;

	UPROPERTY()
	UTextBlock* BodyText = nullptr;

	/** 控件尚未构建时先暂存文案，构建完成后再写入 */
	FText PendingTitle;
	FText PendingBody;
};
