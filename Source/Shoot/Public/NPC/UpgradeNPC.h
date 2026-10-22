// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Dialogue/DialogueDefinitions.h"
#include "Dialogue/DialogueProvider.h"
#include "UpgradeNPC.generated.h"

class UEconomySystem;
class UBaseGameInstance;
class UInteractionDialogueComponent;

/** 升级选项效果类型 */
UENUM(BlueprintType)
enum class EUpgradeEffectType : uint8
{
	BuyShopItem		UMETA(DisplayName = "购买商店道具"),
	AddReputation	UMETA(DisplayName = "增加声誉"),
	AddMoney		UMETA(DisplayName = "增加金钱"),
	UnlockIngredient UMETA(DisplayName = "解锁食材"),
	ClaimLevelGoal	UMETA(DisplayName = "领取关卡目标奖励"),
};

/** 升级选项配置（在 NPC 实例上编辑，也可后续迁移到数据表） */
USTRUCT(BlueprintType)
struct FUpgradeOption
{
	GENERATED_BODY()
public:
	/** 唯一 ID，供蓝图调用 ApplyUpgrade 时指定 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	int32 UpgradeID = 0;

	/** 展示名称（如“高级灶台”） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	FText DisplayName;

	/** 花费的金钱（0 = 免费） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	int32 Cost = 0;

	/** 描述文本 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	FText Description;

	/** 效果类型 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	EUpgradeEffectType EffectType = EUpgradeEffectType::BuyShopItem;

	/** 效果数值：道具 ID（BuyShopItem）/ 声誉值（AddReputation）/ 金钱值（AddMoney）/ 食材 ID（UnlockIngredient） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	int32 EffectValue = 0;
};

/**
 * 升级 / 关卡管理 NPC（角色类，可播放骨骼动画）。
 * 复用通用对话系统：挂载 UInteractionDialogueComponent（自带交互球、提示、对话框），
 * 并实现 IDialogueProvider 提供“升级 / 领取奖励”对话内容。
 * 玩家靠近按 E 打开对话框，离开时对话框自动隐藏。
 */
UCLASS(Blueprintable)
class SHOOT_API AUpgradeNPC : public ACharacter, public IDialogueProvider
{
	GENERATED_BODY()

public:
	AUpgradeNPC();

	/** 通用对话交互组件（自带交互球 + 提示 + 对话框）。可在蓝图子类配置 WidgetClass */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UInteractionDialogueComponent> DialogueComponent;

	/** 可提供的升级选项列表（在编辑器为每个 NPC 实例配置） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	TArray<FUpgradeOption> UpgradeOptions;

	/** 领取关卡目标奖励时给予的额外声誉（仅当关卡目标已达成且未领取时有效） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|LevelGoal")
	int32 LevelGoalRewardReputation = 10;

	/** 是否已经领取过关卡目标奖励（防止重复领取，存于运行时） */
	UPROPERTY(BlueprintReadOnly, Category = "Upgrade|LevelGoal")
	bool bLevelGoalRewardClaimed = false;

	/** 关卡目标是否已达成（来自 UBaseGameInstance::curLevelBP->bGoalAchieved） */
	UFUNCTION(BlueprintCallable, Category = "NPC")
	bool HasLevelGoalAchieved() const;

	/** 是否可以领取关卡目标奖励（已达成且未领取） */
	UFUNCTION(BlueprintCallable, Category = "NPC")
	bool CanClaimLevelGoalReward() const;

	/** 领取关卡目标奖励：达成且未领取时给予声誉奖励，返回是否成功 */
	UFUNCTION(BlueprintCallable, Category = "NPC")
	bool ClaimLevelGoalReward();

	/** 返回当前配置的升级选项（供对话框生成列表） */
	UFUNCTION(BlueprintCallable, Category = "NPC")
	TArray<FUpgradeOption> GetUpgradeOptions() const { return UpgradeOptions; }

	/** 当前金钱是否足够购买某选项 */
	UFUNCTION(BlueprintCallable, Category = "NPC")
	bool CanAfford(int32 UpgradeID) const;

	/** 执行升级：校验后扣费并应用效果，返回是否成功（钱不够等失败返回 false） */
	UFUNCTION(BlueprintCallable, Category = "NPC")
	bool ApplyUpgrade(int32 UpgradeID);

	// —— IDialogueProvider 实现：提供对话内容并处理按钮 ——
	virtual FDialogueData RequestDialogueData_Implementation(APawn* Interactor) override;
	virtual void HandleDialogueButton_Implementation(APawn* Interactor, int32 ButtonID) override;

protected:
	/** 应用单个升级效果（扣费已在调用方处理，BuyShopItem 除外） */
	bool ApplyEffect(const FUpgradeOption& Option);

	/** 获取经济系统（来自 UBaseGameInstance） */
	UEconomySystem* GetEconomy() const;
};
