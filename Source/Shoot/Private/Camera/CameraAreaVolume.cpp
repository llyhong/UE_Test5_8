#include "Camera/CameraAreaVolume.h"
#include "Components/BrushComponent.h"
#include "Engine/CollisionProfile.h"

ACameraAreaVolume::ACameraAreaVolume()
{
	// 只做区域查询，不参与碰撞，玩家可以直接穿过
	GetBrushComponent()->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	GetBrushComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetBrushComponent()->SetGenerateOverlapEvents(false);

	bColored = true;
	BrushColor = FColor(70, 140, 255);
}

bool ACameraAreaVolume::IsLocationInside(const FVector& WorldLocation, float ExtraRadius) const
{
	return EncompassesPoint(WorldLocation, ExtraRadius, nullptr);
}
