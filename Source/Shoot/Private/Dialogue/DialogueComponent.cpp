// Fill out your copyright notice in the Description page of Project Settings.

#include "Dialogue/DialogueComponent.h"
#include "Components/SphereComponent.h"
#include "Components/WidgetComponent.h"
#include "UI/DialogueWidget.h"
#include "Dialogue/DialogueProvider.h"
#include "ShootCharacter.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "DrawDebugHelpers.h"

UInteractionDialogueComponent::UInteractionDialogueComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UInteractionDialogueComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// —— 交互球（运行时挂到 Owner 根组件，玩家靠近才可按 E） ——
	// 注意：必须先 SetupAttachment 再 RegisterComponent，否则组件会先以世界原点注册，
	// 造成“启动时误触发重叠”且玩家站在 NPC 面前却不在球体内（按 E 无反应）。
	InteractionSphere = NewObject<USphereComponent>(Owner, TEXT("DialogueInteractionSphere"));
	if (InteractionSphere)
	{
		InteractionSphere->SetSphereRadius(InteractionRadius);
		InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
		InteractionSphere->SetGenerateOverlapEvents(true);
		InteractionSphere->SetupAttachment(Owner->GetRootComponent());
		InteractionSphere->RegisterComponent();
		InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &UInteractionDialogueComponent::OnInteractionOverlap);
		InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &UInteractionDialogueComponent::OnInteractionEndOverlap);
	}

	// —— 交互提示 Widget（“按 E 对话”，默认隐藏） ——
	if (PromptWidgetClass)
	{
		PromptWidget = NewObject<UWidgetComponent>(Owner, TEXT("DialoguePromptWidget"));
		if (PromptWidget)
		{
			PromptWidget->SetWidgetSpace(EWidgetSpace::Screen);
			PromptWidget->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
			PromptWidget->SetWidgetClass(PromptWidgetClass);
			PromptWidget->SetupAttachment(Owner->GetRootComponent());
			PromptWidget->RegisterComponent();
			PromptWidget->SetVisibility(false);
		}
	}

	// 调试绘制开关由 bDebugDrawRadius 控制（默认关闭）
	SetComponentTickEnabled(bDebugDrawRadius);
}

void UInteractionDialogueComponent::StartDialogue(APawn* Interactor)
{
	if (!DialogueWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Dialogue] StartDialogue 失败: DialogueWidgetClass 未配置 (Owner=%s)"), *GetNameSafe(GetOwner()));
		return;
	}
	if (ActiveDialogueWidget || !Interactor)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(Interactor->GetController());
	if (!PC)
	{
		return;
	}

	ActiveDialogueWidget = CreateWidget<UDialogueWidget>(PC, DialogueWidgetClass);
	if (!ActiveDialogueWidget)
	{
		return;
	}

	ActiveDialogueWidget->DialogueContext = GetOwner();
	ActiveDialogueWidget->OnButtonClicked.AddDynamic(this, &UInteractionDialogueComponent::HandleButtonClicked);

	// 获取对话内容（优先来自 Owner 实现的 IDialogueProvider）
	FDialogueData Data;
	if (IDialogueProvider* Provider = Cast<IDialogueProvider>(GetOwner()))
	{
		Data = Provider->Execute_RequestDialogueData(GetOwner(), Interactor);
	}
	else
	{
		Data.Title = DefaultTitle;
	}

	ActiveDialogueWidget->SetupDialogue(Data);
	ActiveDialogueWidget->AddToViewport();

	// 切换到 UI 交互模式：显示鼠标并能点击对话框按钮。
	// 使用 GameAndUI 而非纯 UIOnly，以保留键盘输入（玩家仍可按 E 关闭对话框）。
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(ActiveDialogueWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
	PC->bShowMouseCursor = true;

	CachedInteractor = Interactor;
	bIsTalking = true;
	OnDialogueStateChanged(true);

	UE_LOG(LogTemp, Log, TEXT("[Dialogue] 玩家按E打开对话框: Owner=%s, 按钮数=%d"), *GetNameSafe(GetOwner()), Data.Buttons.Num());
}

void UInteractionDialogueComponent::CloseDialogue()
{
	if (ActiveDialogueWidget)
	{
		ActiveDialogueWidget->RemoveFromParent();
		ActiveDialogueWidget = nullptr;
	}
	// 恢复游戏输入模式（鼠标隐藏），退出对话交互；先在清空前取出 PlayerController
	if (APlayerController* PC = CachedInteractor ? Cast<APlayerController>(CachedInteractor->GetController()) : nullptr)
	{
		FInputModeGameOnly InputMode;
		PC->SetInputMode(InputMode);
		PC->bShowMouseCursor = false;
	}

	CachedInteractor = nullptr;

	if (bIsTalking)
	{
		bIsTalking = false;
		OnDialogueStateChanged(false);
	}

	UE_LOG(LogTemp, Log, TEXT("[Dialogue] 关闭对话: Owner=%s"), *GetNameSafe(GetOwner()));
}

void UInteractionDialogueComponent::HandleButtonClicked(int32 ButtonID)
{
	// 取消 / 关闭按钮：统一由组件关闭
	if (ButtonID == DIALOGUE_CLOSE_BUTTON_ID)
	{
		CloseDialogue();
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[Dialogue] 玩家点击对话按钮: Owner=%s, ButtonID=%d"), *GetNameSafe(GetOwner()), ButtonID);
	// 业务按钮：转发给 Owner 的 Provider 处理
	if (IDialogueProvider* Provider = Cast<IDialogueProvider>(GetOwner()))
	{
		Provider->Execute_HandleDialogueButton(GetOwner(), CachedInteractor.Get(), ButtonID);
	}

	// 业务处理后刷新对话内容（例如奖励领完、金钱不足按钮置灰）
	if (ActiveDialogueWidget)
	{
		FDialogueData Data;
		if (IDialogueProvider* Provider = Cast<IDialogueProvider>(GetOwner()))
		{
			Data = Provider->Execute_RequestDialogueData(GetOwner(), CachedInteractor.Get());
		}
		else
		{
			Data.Title = DefaultTitle;
		}
		ActiveDialogueWidget->SetupDialogue(Data);
	}
}

void UInteractionDialogueComponent::ShowPrompt()
{
	if (PromptWidget)
	{
		PromptWidget->SetVisibility(true);
	}
}

void UInteractionDialogueComponent::HidePrompt()
{
	if (PromptWidget)
	{
		PromptWidget->SetVisibility(false);
	}
}

void UInteractionDialogueComponent::OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (AShootCharacter* SC = Cast<AShootCharacter>(OtherActor))
	{
		SC->SetOverlappingDialogue(this);
		ShowPrompt();
		UE_LOG(LogTemp, Log, TEXT("[Dialogue] 玩家进入范围: %s -> %s (半径%.0f)"),
			*GetNameSafe(SC), *GetNameSafe(GetOwner()), InteractionRadius);
	}
}

void UInteractionDialogueComponent::OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AShootCharacter* SC = Cast<AShootCharacter>(OtherActor))
	{
		if (SC->GetOverlappingDialogue() == this)
		{
			SC->SetOverlappingDialogue(nullptr);
		}
		HidePrompt();

		// 玩家离开时自动隐藏对话框（满足需求）
		if (bAutoCloseOnLeave)
		{
			UE_LOG(LogTemp, Log, TEXT("[Dialogue] 玩家离开 %s，对话框自动隐藏"), *GetNameSafe(GetOwner()));
			CloseDialogue();
		}
	}
}

void UInteractionDialogueComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bDebugDrawRadius && GetOwner())
	{
		// 每帧绘制交互半径调试球（绿色线框，跟随 Owner 位置，便于调整 InteractionRadius）
		DrawDebugSphere(GetOwner()->GetWorld(), GetOwner()->GetActorLocation(), InteractionRadius, 24, DebugRadiusColor, false, -1.f, 0, 2.f);
	}
}
