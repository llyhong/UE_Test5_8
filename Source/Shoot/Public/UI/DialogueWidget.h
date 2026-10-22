// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Dialogue/DialogueDefinitions.h"
#include "DialogueWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDialogueButtonClicked, int32, ButtonID);

/**
 * 通用对话界面基类：在编辑器里用 WBP 继承它即可实现对话 UI。
 * 蓝图需实现 SetupDialogue 以根据数据创建标题 / 正文 / 按钮，
 * 并为每个按钮的 OnClicked 绑定到 ReportButtonClicked(ButtonID)。
 * “取消”按钮直接传 DIALOGUE_CLOSE_BUTTON_ID(-1) 即可关闭对话。
 */
UCLASS()
class SHOOT_API UDialogueWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 用对话数据填充界面。蓝图实现以创建 / 刷新标题、正文与按钮 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue")
	void SetupDialogue(const FDialogueData& Data);

	/** 按钮被点击时由蓝图按钮的 OnClicked 调用，转发给对话组件 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void ReportButtonClicked(int32 ButtonID);

	/** 按钮点击委托：对话组件监听以分发业务逻辑 */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue")
	FOnDialogueButtonClicked OnButtonClicked;

	/** 关联上下文（通常是拥有该对话的 Actor 或对话组件），蓝图可读取获取更多信息 */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	TObjectPtr<UObject> DialogueContext;
};
