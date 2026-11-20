#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PremiumDishFeedbackWidget.generated.h"

class UImage;
class UTexture2D;
class UWidget;

UCLASS()
class SHOOT_API UPremiumDishFeedbackWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	explicit UPremiumDishFeedbackWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY()
	TObjectPtr<UTexture2D> StarTexture;

	UPROPERTY()
	TObjectPtr<UWidget> RevealLabel;

	UPROPERTY()
	TArray<TObjectPtr<UImage>> RevealStars;

	double RevealStartTime = 0.0;
	bool bRevealFinished = false;
};
