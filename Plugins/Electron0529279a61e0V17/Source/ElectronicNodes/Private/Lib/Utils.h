#pragma once

namespace ENIntersectionHelpers
{
	static bool SegmentIntersection2D(const FVector2f& SegmentAStart, const FVector2f& SegmentAEnd,
									  const FVector2f& SegmentBStart, const FVector2f& SegmentBEnd,
									  FVector2f& OutIntersectionPoint)
	{
		FVector Intersection;
		if (FMath::SegmentIntersection2D(FVector(SegmentAStart.X, SegmentAStart.Y, 0),
										 FVector(SegmentAEnd.X, SegmentAEnd.Y, 0),
										 FVector(SegmentBStart.X, SegmentBStart.Y, 0),
										 FVector(SegmentBEnd.X, SegmentBEnd.Y, 0),
										 Intersection))
		{
			OutIntersectionPoint = FVector2f(static_cast<float>(Intersection.X), static_cast<float>(Intersection.Y));
			return true;
		}
		return false;
	}
}
