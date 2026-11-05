#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "CameraAreaVolume.generated.h"

/**
 * 相机区域体积：玩家进入体积内自动切俯视镜头，离开所有相机区域体积后恢复第三人称。
 * 关卡里可以任意摆放多个、用 Volume 笔刷改形状；删除体积即删除区域。
 */
UCLASS(Blueprintable)
class SHOOT_API ACameraAreaVolume : public AVolume
{
	GENERATED_BODY()

public:
	ACameraAreaVolume();

	/** 关掉后该体积不参与检测（保留摆放与形状，便于调试对比） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Camera|Area")
	bool bCameraAreaActive = true;

	/** 点是否在体积内；ExtraRadius 用于边界回差 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Camera|Area")
	bool IsLocationInside(const FVector& WorldLocation, float ExtraRadius = 0.0f) const;
};
