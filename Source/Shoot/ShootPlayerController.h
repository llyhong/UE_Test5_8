// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ShootPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;


/**
 *  Basic PlayerController class for a third person game
 *  Manages input mappings
 */
UCLASS(abstract)
class AShootPlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** 手柄 IMC（1P/2P 都会挂；2P 只挂它做输入隔离，见 SetupInputComponent）。
	 *  键鼠会广播给所有本地玩家、手柄按键按控制器 ID 路由，所以 2P 不能挂含键鼠绑定的 IMC。
	 *  为空时所有玩家回退 DefaultMappingContexts（行为同旧版）。 */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input|Input Mappings")
	TArray<UInputMappingContext*> GamepadMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

private:
	/** 未配置 GamepadMappingContexts 时，从 DefaultMappingContexts 筛出手柄键位生成运行时 IMC（缓存复用，2P 输入隔离） */
	TArray<UInputMappingContext*> BuildGamepadOnlyContexts();

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> RuntimeGamepadIMC;

};
