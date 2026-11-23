#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DirtyPlateMarkerWidget.generated.h"

class UBorder;

/**
 * 脏碟标记（无文字）：客人留下的空碟上方几只小苍蝇绕圈飞，身后留下虚线飞行轨迹；
 * 碟口冒出两缕臭味飘线。由 AItemActor::ShowDirtyPlateFeedback 创建，碟子被收走 / 换菜时销毁。
 * 全部是图形元素（圆角色块拼的苍蝇 + 轨迹圆点 + 飘线），不含任何文字，避免像 UI 提示卡。
 */
UCLASS()
class SHOOT_API UDirtyPlateMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UDirtyPlateMarkerWidget(const FObjectInitializer& ObjectInitializer);

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	/** 苍蝇：深色身体 + 两片半透明翅膀，每只苍蝇一组 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> FlyBodies;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> FlyWings;

	/** 苍蝇身后的虚线飞行轨迹（小圆点，越靠后越淡），按苍蝇顺序连续存放 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> FlyTrails;

	/** 两缕臭味飘线（圆角细条），循环上浮并淡入渐出 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> StinkWisps;

	float Elapsed = 0.f;
};
