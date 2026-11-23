#include "UI/DirtyPlateMarkerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"

namespace
{
	constexpr int32 FlyCount = 5;
	constexpr float TwoPi = 6.28318530718f;

	// 每只苍蝇一条自己的 8 字轨道：周期 / 中心 / 振幅 / 相位 / 扇翅频率 / 体型
	// 五只速度、相位、体型都错开，避免看起来像同一只苍蝇的分身
	constexpr float FlyLoopSeconds[FlyCount] = { 2.4f, 2.0f, 2.8f, 2.2f, 2.6f };
	const FVector2D FlyCenters[FlyCount] = {
		FVector2D(58.0, 78.0), FVector2D(32.0, 94.0), FVector2D(88.0, 92.0),
		FVector2D(44.0, 60.0), FVector2D(80.0, 66.0) };
	constexpr float FlyRadiusX[FlyCount] = { 18.f, 11.f, 13.f, 14.f, 10.f };
	constexpr float FlyRadiusY[FlyCount] = { 8.f, 5.f, 6.f, 7.f, 5.f };
	constexpr float FlyPhase[FlyCount] = { 0.f, 2.1f, 4.4f, 1.2f, 3.3f };
	constexpr float FlyWingFlapHz[FlyCount] = { 5.5f, 6.4f, 5.0f, 6.0f, 5.8f };
	constexpr float FlyBodyWidth[FlyCount] = { 13.f, 11.f, 11.f, 10.f, 9.f };
	constexpr float FlyBodyHeight[FlyCount] = { 17.f, 14.f, 14.f, 13.f, 12.f };
	constexpr float FlyWingWidth[FlyCount] = { 8.5f, 7.f, 7.f, 6.5f, 6.f };
	constexpr float FlyWingHeight[FlyCount] = { 15.f, 12.f, 12.f, 11.f, 10.f };

	constexpr int32 FlyTrailDotCount[FlyCount] = { 5, 3, 3, 3, 3 };
	constexpr float FlyTrailStep[FlyCount] = { 0.40f, 0.52f, 0.46f, 0.58f, 0.48f };
	constexpr float FlyTrailDotSize = 3.4f;

	constexpr float StinkLoopSeconds = 1.9f;  // 单缕飘线的循环周期
	constexpr float StinkRisePixels = 58.f;   // 飘线一次上浮的距离
	const FVector2D WispPositions[] = { FVector2D(44.f, 94.f), FVector2D(72.f, 98.f) };

	FSlateBrush MakeRoundedBrush(const FLinearColor& Color, float Radius)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Color);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
		return Brush;
	}

	/** 第 FlyIndex 只苍蝇的 8 字形飞行路径（角度单位为弧度） */
	FVector2D FlyPathPos(int32 FlyIndex, float Angle)
	{
		return FlyCenters[FlyIndex] + FVector2D(
			FlyRadiusX[FlyIndex] * FMath::Cos(Angle),
			FlyRadiusY[FlyIndex] * FMath::Sin(Angle * 2.f));
	}

	void PlaceOnCanvas(UCanvasPanel& Canvas, UWidget& Widget, FVector2D Position, FVector2D Size)
	{
		UCanvasPanelSlot* Slot = Canvas.AddChildToCanvas(&Widget);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
	}
}

UDirtyPlateMarkerWidget::UDirtyPlateMarkerWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 每帧都在动画，标记为 volatile，避免被 Slate 失效缓存吞掉重绘
	bIsVolatile = true;
}

TSharedRef<SWidget> UDirtyPlateMarkerWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Canvas;

	// 飘线先建：苍蝇要挡在臭气前面，否则会被飘线盖住
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(WispPositions); ++Index)
	{
		UBorder* Wisp = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
			*FString::Printf(TEXT("DirtyPlateWisp%d"), Index));
		Wisp->SetBrush(MakeRoundedBrush(FLinearColor(0.42f, 0.50f, 0.34f, 0.9f), 3.5f));
		Wisp->SetPadding(FMargin(0.f));
		// 两缕一左一右微微倾斜，避免看起来像规整的竖线
		Wisp->SetRenderTransformAngle(Index == 0 ? -9.f : 11.f);
		PlaceOnCanvas(*Canvas, *Wisp, WispPositions[Index], FVector2D(7.f, 22.f));
		StinkWisps.Add(Wisp);
	}

	// 虚线飞行轨迹：每只苍蝇身后拖一串小圆点，越靠后越淡
	for (int32 Fly = 0; Fly < FlyCount; ++Fly)
	{
		for (int32 DotIndex = 0; DotIndex < FlyTrailDotCount[Fly]; ++DotIndex)
		{
			UBorder* Dot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
				*FString::Printf(TEXT("DirtyPlateFlyTrail%d_%d"), Fly, DotIndex));
			Dot->SetBrush(MakeRoundedBrush(FLinearColor(0.10f, 0.11f, 0.13f, 0.6f), FlyTrailDotSize * 0.5f));
			Dot->SetPadding(FMargin(0.f));
			PlaceOnCanvas(*Canvas, *Dot, FVector2D::ZeroVector, FVector2D(FlyTrailDotSize, FlyTrailDotSize));
			FlyTrails.Add(Dot);
		}
	}

	// 苍蝇身体：深色圆角块，接近椭圆
	for (int32 Fly = 0; Fly < FlyCount; ++Fly)
	{
		UBorder* Body = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
			*FString::Printf(TEXT("DirtyPlateFlyBody%d"), Fly));
		Body->SetBrush(MakeRoundedBrush(FLinearColor(0.05f, 0.05f, 0.06f, 0.96f), FlyBodyWidth[Fly] * 0.5f));
		Body->SetPadding(FMargin(0.f));
		PlaceOnCanvas(*Canvas, *Body, FVector2D::ZeroVector, FVector2D(FlyBodyWidth[Fly], FlyBodyHeight[Fly]));
		FlyBodies.Add(Body);
	}

	// 两片半透明浅色翅膀，一左一右张开
	const float WingAngles[] = { -26.f, 24.f };
	for (int32 Fly = 0; Fly < FlyCount; ++Fly)
	{
		for (int32 WingIndex = 0; WingIndex < UE_ARRAY_COUNT(WingAngles); ++WingIndex)
		{
			UBorder* Wing = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
				*FString::Printf(TEXT("DirtyPlateFlyWing%d_%d"), Fly, WingIndex));
			Wing->SetBrush(MakeRoundedBrush(FLinearColor(0.93f, 0.96f, 1.f, 0.85f), FlyWingWidth[Fly] * 0.5f));
			Wing->SetPadding(FMargin(0.f));
			Wing->SetRenderTransformAngle(WingAngles[WingIndex]);
			// 翅膀绕根部扇动，而不是绕自己的中心
			Wing->SetRenderTransformPivot(FVector2D(0.5f, 0.9f));
			PlaceOnCanvas(*Canvas, *Wing, FVector2D::ZeroVector, FVector2D(FlyWingWidth[Fly], FlyWingHeight[Fly]));
			FlyWings.Add(Wing);
		}
	}

	return Super::RebuildWidget();
}

void UDirtyPlateMarkerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Elapsed += InDeltaTime;

	int32 TrailCursor = 0;
	for (int32 Fly = 0; Fly < FlyCount; ++Fly)
	{
		const float FlyAngle = FlyPhase[Fly] + Elapsed * TwoPi / FlyLoopSeconds[Fly];
		const FVector2D FlyPos = FlyPathPos(Fly, FlyAngle);

		if (FlyBodies.IsValidIndex(Fly) && FlyBodies[Fly])
		{
			FlyBodies[Fly]->SetRenderTranslation(
				FlyPos + FVector2D(-FlyBodyWidth[Fly] * 0.5f, -FlyBodyHeight[Fly] * 0.5f));
			// 身体顺着飞行方向轻微摆动
			FlyBodies[Fly]->SetRenderTransformAngle(10.f * FMath::Sin(FlyAngle * 2.f));
		}

		// 翅膀高频扇动
		const float Flap = 0.35f + 0.65f * FMath::Abs(
			FMath::Sin(Elapsed * TwoPi * FlyWingFlapHz[Fly] + FlyPhase[Fly]));
		for (int32 WingIndex = 0; WingIndex < 2; ++WingIndex)
		{
			const int32 WingItem = Fly * 2 + WingIndex;
			if (!FlyWings.IsValidIndex(WingItem) || !FlyWings[WingItem])
			{
				continue;
			}
			// 左翅从身体左侧向外伸，右翅压在身体上缘
			const float Side = WingIndex == 0
				? -(FlyBodyWidth[Fly] * 0.5f + FlyWingWidth[Fly] * 0.5f)
				: (FlyBodyWidth[Fly] * 0.5f - FlyWingWidth[Fly] * 0.15f);
			FlyWings[WingItem]->SetRenderTranslation(
				FlyPos + FVector2D(Side, -FlyBodyHeight[Fly] * 0.55f));
			FlyWings[WingItem]->SetRenderScale(FVector2D(1.f, Flap));
		}

		for (int32 DotIndex = 0; DotIndex < FlyTrailDotCount[Fly]; ++DotIndex)
		{
			if (FlyTrails.IsValidIndex(TrailCursor) && FlyTrails[TrailCursor])
			{
				const FVector2D DotPos = FlyPathPos(Fly, FlyAngle - (DotIndex + 1) * FlyTrailStep[Fly]);
				const float Age = static_cast<float>(DotIndex + 1) / static_cast<float>(FlyTrailDotCount[Fly] + 1);
				FlyTrails[TrailCursor]->SetRenderTranslation(
					DotPos + FVector2D(-FlyTrailDotSize * 0.5f, -FlyTrailDotSize * 0.5f));
				FlyTrails[TrailCursor]->SetRenderOpacity(0.6f * (1.f - Age));
			}
			++TrailCursor;
		}
	}

	for (int32 Index = 0; Index < StinkWisps.Num(); ++Index)
	{
		UBorder* Wisp = StinkWisps[Index];
		if (!Wisp)
		{
			continue;
		}
		// 两缕错开半拍，形成“持续冒烟”的感觉
		const float Cycle = FMath::Fmod(Elapsed / StinkLoopSeconds + Index * 0.5f, 1.f);
		const float Sway = FMath::Sin(Cycle * TwoPi * 1.6f + Index * 1.3f);
		Wisp->SetRenderTranslation(FVector2D(4.5f * Sway, -StinkRisePixels * Cycle));
		Wisp->SetRenderScale(FVector2D(1.f, 0.7f + 0.5f * Cycle));
		Wisp->SetRenderOpacity(0.95f * FMath::Sin(PI * Cycle));
	}
}
