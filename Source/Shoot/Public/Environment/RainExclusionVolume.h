#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RainExclusionVolume.generated.h"

class UBoxComponent;

/**
 * 禁雨区域：盒体范围内不显示程序化雨丝（如就餐区、煮饭区）。
 * 在关卡里摆放并缩放盒体即可生效，可随时增删；雨效剔除逻辑见 UStylizedSkyBridge。
 */
UCLASS(Blueprintable)
class SHOOT_API ARainExclusionVolume : public AActor
{
	GENERATED_BODY()

public:
	ARainExclusionVolume();

	/** 禁雨范围（编辑器里直接缩放本 Actor 即可调整大小） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Rain")
	TObjectPtr<UBoxComponent> Bounds;

	/** 关掉后该区域不参与雨丝剔除（保留摆放，便于调试对比） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Rain")
	bool bExcludeRain = true;

	/** 世界空间包围盒（供雨丝逐实例剔除使用） */
	FBox GetBoundsBox() const;
};
