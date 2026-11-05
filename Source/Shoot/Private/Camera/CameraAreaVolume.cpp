#include "Camera/CameraAreaVolume.h"
#include "Components/BrushComponent.h"
#include "Engine/CollisionProfile.h"

ACameraAreaVolume::ACameraAreaVolume()
{
	// 必须是 QueryOnly：AVolume::EncompassesPoint 走 GetSquaredDistanceToCollision，
	// NoCollision 时没有查询体，永远返回 false。全通道 Ignore 保证不阻挡任何东西。
	GetBrushComponent()->SetCollisionObjectType(ECC_WorldStatic);
	GetBrushComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GetBrushComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
	GetBrushComponent()->SetGenerateOverlapEvents(false);

	bColored = true;
	BrushColor = FColor(70, 140, 255);
}

bool ACameraAreaVolume::IsLocationInside(const FVector& WorldLocation, float ExtraRadius) const
{
	return EncompassesPoint(WorldLocation, ExtraRadius, nullptr);
}
