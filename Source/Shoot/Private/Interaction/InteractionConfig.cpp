// Fill out your copyright notice in the Description page of Project Settings.

#include "Interaction/InteractionConfig.h"

#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

TAutoConsoleVariable<int32> CVarDebugInteract(
	TEXT("Shoot.DebugInteract"),
	0,
	TEXT("绘制所有交互判定球/盒。0=关闭 1=开启。颜色：绿=当前目标 白=范围内 红=被遮挡 灰=无人靠近"),
	ECVF_Cheat);

// ============================================================================
// E 提示归属：统一提示（每玩家一份） vs 设备自带 TipsWidget
// ============================================================================
namespace
{
	TAutoConsoleVariable<int32> CVarDeviceOwnPromptTips(
		TEXT("Shoot.DeviceOwnPromptTips"),
		0,
		TEXT("设备自带的 WBP_Prompt TipsWidget 是否显示。0=关闭（E 提示统一由角色显示，设备控件只做位置锚点，适配分屏）；1=恢复设备自己显示的旧行为"),
		ECVF_Cheat);
}

namespace InteractionPromptTuning
{
	bool UseDeviceOwnPromptTips()
	{
		return CVarDeviceOwnPromptTips.GetValueOnGameThread() > 0;
	}
}

// ============================================================================
// 运行时交互半径倍率
// ============================================================================
// 这里用 FAutoConsoleVariableRef 而不是带回调的版本：回调时机在控制台执行线程/时机上不好把握，
// 改成"角色每 0.15s 的候选刷新里顺便比对一次"，倍率变了就重设世界上所有交互球。
// 好处是刷新一定发生在游戏线程、且绝对不会在关卡加载中途去碰组件。
namespace
{
	float GInteractionScale = 1.f;
	float GInteractionFacingPenalty = InteractFacingPenalty;

	FAutoConsoleVariableRef CVarInteractionScale(
		TEXT("Shoot.InteractScale"),
		GInteractionScale,
		TEXT("交互判定半径的运行时倍率（1=按 InteractionConfig 基准值）。例如 Shoot.InteractScale 0.8 收紧、1.3 放宽，改完 0.15s 内自动生效，无需重编。"),
		ECVF_Cheat);

	FAutoConsoleVariableRef CVarInteractionFacing(
		TEXT("Shoot.InteractFacing"),
		GInteractionFacingPenalty,
		TEXT("交互朝向权重：目标偏离玩家正前方时的距离惩罚（0=只看距离，1.2=正后方按 2.2 倍距离算）。立即生效。"),
		ECVF_Cheat);
}

namespace InteractionFacing
{
	float GetPenaltyScale()
	{
		return FMath::Clamp(GInteractionFacingPenalty, 0.f, 4.f);
	}
}

namespace InteractionScale
{
	float Get()
	{
		// 钳制：防止 0 或离谱数值把判定球改没/改爆
		return FMath::Clamp(GInteractionScale, 0.25f, 3.f);
	}

	void RefreshAllInteractionScales(UWorld* World)
	{
		if (!World)
		{
			return;
		}

		// 逐个 Actor 找它自己（或它的组件）身上的 ApplyInteractionScale()，
		// 用反射调用而不是 Cast 到 11 个具体类型：新增交互物件时只要照抄这个函数名就自动被覆盖。
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor) || Actor->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}

			if (UFunction* Fn = Actor->FindFunction(TEXT("ApplyInteractionScale")))
			{
				Actor->ProcessEvent(Fn, nullptr);
			}

			TInlineComponentArray<UActorComponent*> Components(Actor);
			for (UActorComponent* Comp : Components)
			{
				if (!Comp || Comp->HasAnyFlags(RF_ClassDefaultObject))
				{
					continue;
				}
				if (UFunction* Fn = Comp->FindFunction(TEXT("ApplyInteractionScale")))
				{
					Comp->ProcessEvent(Fn, nullptr);
				}
			}
		}
	}
}
