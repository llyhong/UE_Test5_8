// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "CharacterSelectWidget.generated.h"

class UBaseGameInstance;
class UBorder;
class UHorizontalBox;
class UImage;
class UTextBlock;

/** 选角卡片按钮：记录角色 ID，点击时通知所属选角界面（支持表里继续加行） */
UCLASS()
class SHOOT_API URoleSelectCardButton : public UButton
{
	GENERATED_BODY()

public:
	URoleSelectCardButton();

	/** 绑定角色 ID / 所属界面，并挂上点击回调（由选角界面在生成卡片时调用） */
	void InitCard(int32 InRoleID, class UCharacterSelectWidget* InOwner);

	/** 卡片对应的角色 ModelID */
	int32 RoleID = 0;

	/** 所属选角界面（弱引用，避免控件互相持有） */
	TWeakObjectPtr<class UCharacterSelectWidget> OwnerWidget;

private:
	UFUNCTION()
	void HandleClicked();
};

/**
 * 选角界面（C++ 构建，无需 WBP）：
 *  - 角色列表完全由 DT_CharacterRoles（RowStruct = FRoleInfo，bSelectable=true 的行）驱动；
 *  - 鼠标点击卡片 / ←→（或 A D）切换，点击「开始经营」或回车/空格确认；
 *  - 确认后由 UBaseGameInstance::ConfirmRoleSelection 写入存档、切换玩家外观并关闭界面。
 *
 * 入口：UBaseGameInstance::OnWorldChanged 在进入 CharacterSelectMapName（默认 Kitchen_New）
 * 且本会话尚未选角时自动创建，因此登录（L_StartMenu）→ 选角 → 主关卡的链路无需改蓝图。
 */
UCLASS()
class SHOOT_API UCharacterSelectWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UCharacterSelectWidget(const FObjectInitializer& ObjectInitializer);

	/** 打开界面时指定预选角色（0 = 自动选第一个可选角色） */
	void SetInitialRole(int32 RoleID);

	/** 选中某个角色并刷新界面 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Role")
	void SelectRole(int32 RoleID);

	/** 当前界面上选中的角色 ModelID */
	UFUNCTION(BlueprintPure, Category = "Shoot|Role")
	int32 GetCurrentRoleID() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	/** 按配置生成一张角色卡片 */
	void BuildCard(int32 CardIndex, int32 RoleID);

	/** 刷新卡片高亮与右下角角色信息 */
	void RefreshUI();

	UFUNCTION()
	void OnConfirmClicked();

	UPROPERTY()
	TObjectPtr<UBaseGameInstance> GameInstance;

	UPROPERTY()
	TObjectPtr<UHorizontalBox> CardRow;

	UPROPERTY()
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY()
	TObjectPtr<UTextBlock> PersonalityText;

	UPROPERTY()
	TObjectPtr<UTextBlock> LikesText;

	UPROPERTY()
	TObjectPtr<UTextBlock> DescriptionText;

	UPROPERTY()
	TObjectPtr<UButton> ConfirmButton;

	/** 与 RoleIDs 一一对应的卡片控件 */
	UPROPERTY()
	TArray<TObjectPtr<URoleSelectCardButton>> CardButtons;

	UPROPERTY()
	TArray<TObjectPtr<UBorder>> CardBorders;

	UPROPERTY()
	TArray<TObjectPtr<UImage>> CardIcons;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> CardNameTexts;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> CardPersonalityTexts;

	/** 可选角色 ModelID（与卡片顺序一致） */
	TArray<int32> RoleIDs;

	int32 CurrentRoleID = 0;
	int32 InitialRoleID = 0;
};
