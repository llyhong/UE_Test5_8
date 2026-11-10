#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BusinessSignWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UTextBlock;
class USoundBase;

UCLASS()
class SHOOT_API UBusinessSignWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeSign(bool bOpen, float Seconds, const FText& OpenText, const FText& ClosedText,
		const FText& OpenDescription, const FText& ClosedDescription, USoundBase* Sound,
		float Volume, bool bPlaySound);

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|BusinessSign")
	float AnimationProgress = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|BusinessSign")
	bool bShowingOpenSide = false;

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|BusinessSign")
	bool bFlipSoundPlayed = false;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void ApplySide(bool bOpen);

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> HangingSign;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Plaque;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Face;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Title;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Description;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> FlipSound;

	FText OpenLabel;
	FText ClosedLabel;
	FText OpenSubtitle;
	FText ClosedSubtitle;
	float Duration = 3.f;
	double StartedAt = 0.0;
	float SoundVolume = 0.7f;
	bool bTargetOpen = false;
	bool bShouldPlaySound = false;
};
