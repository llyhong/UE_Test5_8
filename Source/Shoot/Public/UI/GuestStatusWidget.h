// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NPC/GuestTypes.h"
#include "GuestStatusWidget.generated.h"

class UTextBlock;
class UImage;
class UDataTable;
class UTexture2D;
class AGuestCharacter;

/** 配置表行：某一客人状态对应的候选台词（随机抽一句）+ 颜色 + 贴图 */
USTRUCT(BlueprintType)
struct FGuestStatusRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 该配置行适用的客人状态 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Status")
	EGuestState State = EGuestState::Waiting;

	/** 是否为“已吃饱满意”的离场专属行（仅 Leaving 状态区分；其余状态忽略） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Status")
	bool bSatisfied = false;

	/** 该状态的候选台词，随机抽一句显示 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Status")
	TArray<FText> Lines;

	/** 该状态对应的显示颜色（文本 / 气泡主色） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Status")
	FLinearColor Color = FLinearColor::White;

	/** 该状态对应的贴图（如生气表情）；为空则不显示图标 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Status")
	TObjectPtr<UTexture2D> Icon = nullptr;
};

/**
 * 客人头顶状态气泡的基类（纯接口，不含文本/颜色/图标等具体显示，但提供通用呈现行为）。
 * 显示内容由蓝图负责：
 *  - 蓝图（继承本类）里摆放并命名为 TextBlock / IconImage 的控件，通过 BindWidget 注入；
 *  - 蓝图在 OnOwnerCharacterSet 里绑定 OwnerCharacter 的 OnGuestStatusUpdated，回调中调用
 *    GetRandomStatusDisplay(State, bSatisfied) 按状态从配置表随机抽取台词 / 颜色 / 贴图，
 *    再设到 TextBlock 与 IconImage 上。
 * 通用呈现：默认开启“聊天气泡”式呼吸渐隐渐显（bAutoFade），由本类统一处理整体不透明度。
 */
UCLASS()
class SHOOT_API UGuestStatusWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** 拥有本头顶气泡的客人（由 AGuestCharacter 在状态变化时主动赋值）；蓝图可直接取用，也可自行 Cast */
	UPROPERTY(BlueprintReadOnly, Category = "Guest|Status")
	TObjectPtr<AGuestCharacter> OwnerCharacter;

	/** C++ 在把 OwnerCharacter 赋值完成后调用一次，蓝图可在此安全地绑定 OwnerCharacter 的 OnGuestStatusUpdated（Construct 时 OwnerCharacter 尚为 null，故不能在 Construct 里绑定） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Guest|Status")
	void OnOwnerCharacterSet();


	/** 是否启用“聊天气泡”式呼吸渐隐渐显（周期性淡入淡出）；为 false 则保持常显 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Status")
	bool bAutoFade = true;

	/** 一次“渐隐 + 渐显”的完整周期（秒）；若下方 Min/Max 区间无效（Max<=Min）则用作固定周期 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Status", meta = (EditCondition = "bAutoFade", ClampMin = "0.5"))
	float FadePeriod = 4.0f;

	/** 呼吸周期随机区间下限（秒）：每位客人在 [Min, Max] 内随机取一个周期，避免所有气泡同步闪烁 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Status", meta = (EditCondition = "bAutoFade", ClampMin = "0.5"))
	float FadePeriodMin = 2.5f;

	/** 呼吸周期随机区间上限（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Status", meta = (EditCondition = "bAutoFade", ClampMin = "0.5"))
	float FadePeriodMax = 6.0f;

	/** 最暗时的不透明度（0=完全隐藏，0.15=半透明呼吸） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Status", meta = (EditCondition = "bAutoFade", ClampMin = "0", ClampMax = "1"))
	float MinOpacity = 0.15f;

	/** 最亮时的不透明度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Status", meta = (EditCondition = "bAutoFade", ClampMin = "0", ClampMax = "1"))
	float MaxOpacity = 1.0f;

	/** 是否自动周期性切换台词（每隔随机一段时间，通知蓝图重新抽一句，营造“聊天气泡”频繁换话的感觉） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Status")
	bool bAutoSwitchLine = true;

	/** 自动切换台词的间隔随机区间下限（秒）：每位客人不同，且每次切换后重新随机，避免整齐划一 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Status", meta = (EditCondition = "bAutoSwitchLine", ClampMin = "0.5"))
	float LineSwitchMin = 4.0f;

	/** 自动切换台词的间隔随机区间上限（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guest|Status", meta = (EditCondition = "bAutoSwitchLine", ClampMin = "0.5"))
	float LineSwitchMax = 10.0f;

	/** 周期性切换台词时通知蓝图：蓝图应读取 OwnerCharacter 当前状态，重新从配置表抽一句并刷新文本/图标 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Guest|Status")
	void OnSwitchLine();

protected:
	virtual void NativeConstruct() override;

	/** 周期性按正弦曲线调节整体不透明度，形成呼吸 / 聊天气泡效果 */
	void TickFade();

	/** 到达随机间隔后通知蓝图切换台词，并重新排定下一次（间隔再次随机） */
	void TriggerLineSwitch();

	/** 在 [LineSwitchMin, LineSwitchMax] 内取随机间隔，排定下一次切换 */
	void ScheduleLineSwitch();

	FTimerHandle FadeTimerHandle;
	float FadeElapsed = 0.f;

	/** 本次实例实际使用的呼吸周期（NativeConstruct 时按区间随机得到） */
	float EffectiveFadePeriod = 4.0f;

	FTimerHandle LineSwitchTimerHandle;
};
