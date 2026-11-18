// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShootPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Shoot.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "Game/BaseGameInstance.h"
#include "Game/BaseLevelBP.h"
#include "Game/TutorialTaskManager.h"
#include "UI/SupplyWarningWidget.h"
#include "Kismet/GameplayStatics.h"

void AShootPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// only spawn touch controls on local player controllers
	if (SVirtualJoystick::ShouldDisplayTouchInterface() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogShoot, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}

	// 每本地玩家一份低库存提醒（分屏时不会共享同一份控件）
	if (IsLocalPlayerController())
	{
		USupplyWarningWidget::EnsureForPlayer(this);

		// 关卡里没摆任务管理器时自动补一个，保证新手引导开箱可用（配置表由 CDO 指定）
		if (GetWorld() && !UGameplayStatics::GetActorOfClass(this, ATutorialTaskManager::StaticClass()))
		{
			GetWorld()->SpawnActor<ATutorialTaskManager>(ATutorialTaskManager::StaticClass(), FTransform::Identity);
		}
	}
}

void AShootPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			// 双人分屏输入隔离：手柄按键按控制器 ID 只路由给对应玩家，键鼠会广播给所有玩家。
			// 所以 2P 只挂手柄 IMC——没配 GamepadMappingContexts 时，运行时从 Default IMC
			// 剥出纯手柄键位自建一份，避免改蓝图资产；1P = 键鼠 IMC + 手柄 IMC（双手柄/单人手柄都能用）
			const ULocalPlayer* LP = GetLocalPlayer();
			const bool bExtraPlayer = LP && LP->GetControllerId() > 0;
			if (!bExtraPlayer)
			{
				for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
			TArray<UInputMappingContext*> PadContexts = GamepadMappingContexts;
			if (PadContexts.Num() == 0)
			{
				PadContexts = BuildGamepadOnlyContexts();
			}
			for (UInputMappingContext* CurrentContext : PadContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!SVirtualJoystick::ShouldDisplayTouchInterface())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}

TArray<UInputMappingContext*> AShootPlayerController::BuildGamepadOnlyContexts()
{
	if (RuntimeGamepadIMC)
	{
		return { RuntimeGamepadIMC };
	}

	RuntimeGamepadIMC = NewObject<UInputMappingContext>(this);
	int32 Added = 0;
	for (UInputMappingContext* Ctx : DefaultMappingContexts)
	{
		if (!Ctx)
		{
			continue;
		}
		for (const FEnhancedActionKeyMapping& Mapping : Ctx->GetMappings())
		{
			if (Mapping.Key.IsGamepadKey())
			{
				FEnhancedActionKeyMapping& NewMapping = RuntimeGamepadIMC->MapKey(Mapping.Action, Mapping.Key);
				NewMapping.Modifiers = Mapping.Modifiers;
				NewMapping.Triggers = Mapping.Triggers;
				Added++;
			}
		}
	}
	UE_LOG(LogShoot, Log, TEXT("Built runtime gamepad-only IMC: %d mappings"), Added);
	return { RuntimeGamepadIMC };
}
