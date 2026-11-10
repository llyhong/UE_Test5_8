#include "Environment/RainExclusionVolume.h"
#include "Components/BoxComponent.h"

ARainExclusionVolume::ARainExclusionVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	RootComponent = Bounds;
	Bounds->SetBoxExtent(FVector(600.f, 600.f, 900.f));
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bounds->SetGenerateOverlapEvents(false);
	// 视口里未选中也画出盒体，摆放关卡时能直接目测禁雨范围
	Bounds->ShapeColor = FColor(70, 150, 255);
	Bounds->bDrawOnlyIfSelected = false;
}

FBox ARainExclusionVolume::GetBoundsBox() const
{
	if (!Bounds)
	{
		return FBox(ForceInit);
	}

	return Bounds->CalcBounds(Bounds->GetComponentTransform()).GetBox();
}
