// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/UpgradeNPC.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dialogue/DialogueComponent.h"
#include "Game/BaseGameInstance.h"
#include "Game/BaseLevelBP.h"
#include "Game/EconomySystem.h"

AUpgradeNPC::AUpgradeNPC()
{
	PrimaryActorTick.bCanEverTick = false;

	// 角色不使用自带的移动输入（原地站立的 NPC），关闭自动寻路移动
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->SetMovementMode(MOVE_None);
	}

	// 通用对话组件（自带交互球 + 提示 + 对话框），处理按 E 对话与离开自动关闭。
	// 可在蓝图子类里配置 DialogueWidgetClass / PromptWidgetClass。
	DialogueComponent = CreateDefaultSubobject<UInteractionDialogueComponent>(TEXT("DialogueComponent"));
}

UEconomySystem* AUpgradeNPC::GetEconomy() const
{
	if (const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		return GI->GetEconomySystem();
	}
	return nullptr;
}

bool AUpgradeNPC::HasLevelGoalAchieved() const
{
	if (const UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
	{
		if (GI->curLevelBP)
		{
			return GI->curLevelBP->bGoalAchieved;
		}
	}
	return false;
}

bool AUpgradeNPC::CanClaimLevelGoalReward() const
{
	return HasLevelGoalAchieved() && !bLevelGoalRewardClaimed;
}

bool AUpgradeNPC::ClaimLevelGoalReward()
{
	if (!CanClaimLevelGoalReward())
	{
		return false;
	}
	if (UEconomySystem* Eco = GetEconomy())
	{
		Eco->AddReputation(LevelGoalRewardReputation);
		bLevelGoalRewardClaimed = true;
		return true;
	}
	return false;
}

bool AUpgradeNPC::CanAfford(int32 UpgradeID) const
{
	const FUpgradeOption* Found = UpgradeOptions.FindByPredicate(
		[UpgradeID](const FUpgradeOption& O) { return O.UpgradeID == UpgradeID; });
	if (!Found)
	{
		return false;
	}
	if (const UEconomySystem* Eco = GetEconomy())
	{
		return Eco->GetMoney() >= Found->Cost;
	}
	return false;
}

bool AUpgradeNPC::ApplyUpgrade(int32 UpgradeID)
{
	const FUpgradeOption* Found = UpgradeOptions.FindByPredicate(
		[UpgradeID](const FUpgradeOption& O) { return O.UpgradeID == UpgradeID; });
	if (!Found)
	{
		return false;
	}
	if (!CanAfford(UpgradeID))
	{
		return false;
	}

	// 购买商店道具：BuyItem 内部已处理扣费，无需再 SpendMoney
	if (Found->EffectType == EUpgradeEffectType::BuyShopItem)
	{
		if (UEconomySystem* Eco = GetEconomy())
		{
			return Eco->BuyItem(Found->EffectValue, Found->Cost);
		}
		return false;
	}

	// 其它类型：先扣费，再应用效果
	if (UEconomySystem* Eco = GetEconomy())
	{
		if (!Eco->SpendMoney(Found->Cost, EMoneySource::ShopPurchase))
		{
			return false;
		}
		return ApplyEffect(*Found);
	}
	return false;
}

bool AUpgradeNPC::ApplyEffect(const FUpgradeOption& Option)
{
	if (UEconomySystem* Eco = GetEconomy())
	{
		switch (Option.EffectType)
		{
		case EUpgradeEffectType::AddReputation:
			Eco->AddReputation(Option.EffectValue);
			return true;
		case EUpgradeEffectType::AddMoney:
			// 奖励金钱用 Debug 来源，不计入关卡“金钱目标”
			Eco->AddMoney(Option.EffectValue, EMoneySource::Debug);
			return true;
		case EUpgradeEffectType::UnlockIngredient:
			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
			{
				GI->UnlockIngredients({ Option.EffectValue });
				return true;
			}
			return false;
		default:
			return false;
		}
	}
	return false;
}

FDialogueData AUpgradeNPC::RequestDialogueData_Implementation(APawn* Interactor)
{
	FDialogueData Data;
	Data.Title = FText::FromString(TEXT("升级台"));
	Data.Body = FText::FromString(TEXT("欢迎，选择你要进行的操作："));

	// 领取关卡奖励（仅当可领取时显示）
	if (CanClaimLevelGoalReward())
	{
		FDialogueButton Btn;
		Btn.Label = FText::Format(FText::FromString(TEXT("领取关卡奖励 (+{0} 声誉)")), FText::AsNumber(LevelGoalRewardReputation));
		Btn.ButtonID = 0;
		Data.Buttons.Add(Btn);
	}

	// 升级选项（ID = 索引 + 1；钱不够时按钮置灰）
	for (int32 i = 0; i < UpgradeOptions.Num(); ++i)
	{
		const FUpgradeOption& Opt = UpgradeOptions[i];
		FDialogueButton Btn;
		Btn.Label = FText::Format(FText::FromString(TEXT("{0}  (花费 {1})")), Opt.DisplayName, FText::AsNumber(Opt.Cost));
		Btn.ButtonID = i + 1;
		Btn.bEnabled = CanAfford(Opt.UpgradeID);
		Data.Buttons.Add(Btn);
	}

	// 取消按钮（约定 ID = -1，由对话组件统一关闭）
	FDialogueButton Cancel;
	Cancel.Label = FText::FromString(TEXT("取消"));
	Cancel.ButtonID = DIALOGUE_CLOSE_BUTTON_ID;
	Data.Buttons.Add(Cancel);

	// 关卡目标已达成：提供“进入下一关”入口（在同一张大地图内切换 LevelID / 目标 / 订单）
	if (HasLevelGoalAchieved())
	{
		FDialogueButton Next;
		Next.Label = FText::FromString(TEXT("进入下一关"));
		Next.ButtonID = DIALOGUE_ADVANCE_LEVEL_BUTTON_ID;
		Data.Buttons.Add(Next);
	}

	return Data;
}

void AUpgradeNPC::HandleDialogueButton_Implementation(APawn* Interactor, int32 ButtonID)
{
	if (ButtonID == 0)
	{
		// 领取关卡奖励
		ClaimLevelGoalReward();
	}
	else if (ButtonID >= 1 && ButtonID <= UpgradeOptions.Num())
	{
		// 购买 / 执行升级（ButtonID - 1 为升级选项索引）
		ApplyUpgrade(UpgradeOptions[ButtonID - 1].UpgradeID);
	}
	else if (ButtonID == DIALOGUE_ADVANCE_LEVEL_BUTTON_ID)
	{
		// 进入下一关：切换到 LevelID + 1（同一张大地图，目标与订单随之切换）
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
		{
			if (ABaseLevelBP* Level = GI->curLevelBP)
			{
				Level->AdvanceToLevel(Level->LevelID + 1);
			}
		}
		// 重置本 NPC 的奖励领取标记，使下一关达成后仍可再次领取
		bLevelGoalRewardClaimed = false;
	}
	// -1（取消）由对话组件处理关闭；其它 ID 忽略
}
