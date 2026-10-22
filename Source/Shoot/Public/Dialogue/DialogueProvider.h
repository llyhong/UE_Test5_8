// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Dialogue/DialogueDefinitions.h"
#include "DialogueProvider.generated.h"

class APawn;

/** 对话内容提供接口：任何想拥有对话能力的 Actor（NPC 或后续新增角色）都应实现它 */
UINTERFACE(MinimalAPI, Blueprintable)
class UDialogueProvider : public UInterface
{
	GENERATED_BODY()
};

class SHOOT_API IDialogueProvider
{
	GENERATED_BODY()

public:
	/** 提供对话内容（标题 / 正文 / 按钮）。C++ 与蓝图均可实现 */
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue")
	FDialogueData RequestDialogueData(APawn* Interactor);

	/** 处理按钮点击（ButtonID 由按钮定义给出） */
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue")
	void HandleDialogueButton(APawn* Interactor, int32 ButtonID);
};
