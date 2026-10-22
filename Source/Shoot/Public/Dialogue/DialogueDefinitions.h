// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DialogueDefinitions.generated.h"

/** 单个对话按钮定义（数据驱动，蓝图据此动态生成按钮） */
USTRUCT(BlueprintType)
struct SHOOT_API FDialogueButton
{
	GENERATED_BODY()

public:
	/** 按钮显示文本 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FText Label;

	/** 按钮标识 ID，点击时回传给 Provider（约定 -1 为“取消/关闭”，由组件统一处理） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	int32 ButtonID = 0;

	/** 是否可点击（如金钱不足的升级按钮可置灰） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	bool bEnabled = true;
};

/** 一次对话的完整内容（标题 + 正文 + 按钮列表） */
USTRUCT(BlueprintType)
struct SHOOT_API FDialogueData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FText Body;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TArray<FDialogueButton> Buttons;
};

/** 约定：取消 / 关闭按钮使用此 ID，由对话组件统一处理关闭 */
static constexpr int32 DIALOGUE_CLOSE_BUTTON_ID = -1;

/** 约定：进入下一关按钮使用此 ID，由升级 NPC 转发给 BaseLevelBP::AdvanceToLevel 处理 */
static constexpr int32 DIALOGUE_ADVANCE_LEVEL_BUTTON_ID = -2;
