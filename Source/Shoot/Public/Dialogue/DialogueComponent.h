// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dialogue/DialogueDefinitions.h"
#include "DialogueComponent.generated.h"

class USphereComponent;
class UWidgetComponent;
class UDialogueWidget;
class APawn;

/**
 * 通用“对话交互”组件：可挂到任何 Actor / Character 上，使其拥有：
 *   - 交互球（玩家靠近才可按 E 对话）
 *   - “按 E 对话”提示 Widget
 *   - 按 E 打开通用对话界面（数据来自 Owner 实现的 IDialogueProvider）
 *   - 玩家离开交互范围时自动关闭对话（bAutoCloseOnLeave）
 *   - 确认 / 取消等按钮（数据驱动，取消按钮由组件统一关闭）
 * 后续新增的角色类只需挂载本组件并实现 IDialogueProvider 即可复用整套对话能力。
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SHOOT_API UInteractionDialogueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractionDialogueComponent();

	/** 对话界面 Widget 类（蓝图继承 UDialogueWidget） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TSubclassOf<UDialogueWidget> DialogueWidgetClass;

	/** 交互球半径 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	float InteractionRadius = 150.f;

	/** 是否绘制交互半径调试球（绿色线框），便于在编辑器 / 运行时调整 InteractionRadius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Debug")
	bool bDebugDrawRadius = false;

	/** 调试球颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Debug")
	FColor DebugRadiusColor = FColor::Green;

	/** 交互提示 Widget 类（“按 E 对话”） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TSubclassOf<UUserWidget> PromptWidgetClass;

	/** 玩家离开交互范围时是否自动关闭对话（满足“离开自动隐藏”需求） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	bool bAutoCloseOnLeave = true;

	/** 当 Owner 未实现 IDialogueProvider 时使用的默认标题（可选） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FText DefaultTitle;

	/** 开始对话（玩家按 E 触发）。已对话时忽略 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void StartDialogue(APawn* Interactor);

	/** 关闭对话（取消按钮或程序调用；玩家离开时也会调用） */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void CloseDialogue();

	/** 当前对话框是否处于打开状态（供切换开/关使用） */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	bool IsDialogueOpen() const { return ActiveDialogueWidget != nullptr; }

	/** 是否在对话中（Owner 的 AnimBP 可读取以切换待机 / 交谈动画） */
	UPROPERTY(BlueprintReadWrite, Category = "Dialogue")
	bool bIsTalking = false;

	/** 蓝图实现：开始 / 结束对话时驱动待机 / 交谈动画 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue")
	void OnDialogueStateChanged(bool bOpen);

protected:
	virtual void BeginPlay() override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 交互球：玩家进入时向其注册本组件并显示提示 */
	UFUNCTION()
	void OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** 交互球：玩家离开时反注册本组件，并按需自动关闭对话 */
	UFUNCTION()
	void OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** 对话界面按钮点击回调 */
	UFUNCTION()
	void HandleButtonClicked(int32 ButtonID);

	void ShowPrompt();
	void HidePrompt();

	UPROPERTY()
	TObjectPtr<USphereComponent> InteractionSphere;

	UPROPERTY()
	TObjectPtr<UWidgetComponent> PromptWidget;

	UPROPERTY()
	TObjectPtr<UDialogueWidget> ActiveDialogueWidget;

	UPROPERTY()
	TObjectPtr<APawn> CachedInteractor;
};
