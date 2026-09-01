#pragma once

#include "UnrealPerformerApi.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/ActorComponent.h"
#include "Styling/SlateBrush.h"
#include "GodfreyListenCueComponent.generated.h"

class UGodfreyVoiceInputComponent;
class UTexture2D;
class SOverlay;
class SImage;
class STextBlock;
class SBorder;

/**
 * Top-right brass signal lantern: dim red + "Wait" while Godfrey speaks / post-speech ignore,
 * soft green + "Speak" when the visitor mic would accept speech. Period label typeface (Constantia).
 */
UCLASS(ClassGroup = (Godfrey), meta = (BlueprintSpawnableComponent))
class UNREAL_PERFORMER_API UGodfreyListenCueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGodfreyListenCueComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Listen Cue")
	bool bShowCue = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Listen Cue")
	bool bShowLabels = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Listen Cue", meta = (MultiLine = "true"))
	FString SpeakWhileWaitWarningText = TEXT("He cannot hear you. Speak only when the lantern is green.");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Listen Cue", meta = (ClampMin = "10", ClampMax = "28"))
	float WarningFontSize = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Listen Cue", meta = (ClampMin = "48", ClampMax = "256"))
	float LanternSize = 144.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Listen Cue", meta = (ClampMin = "0", ClampMax = "64"))
	float Margin = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Listen Cue", meta = (ClampMin = "10", ClampMax = "48"))
	float LabelFontSize = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Listen Cue", meta = (ClampMin = "4", ClampMax = "32"))
	float BackingPadding = 12.f;

private:
	void EnsureOverlay();
	void TearDownOverlay();
	void UpdateCueVisual(bool bCanSpeak, bool bSpeakWhileWaitWarning);
	bool ResolveCanVisitorSpeak() const;
	bool ResolveSpeakWhileWaitWarning() const;
	UGodfreyVoiceInputComponent* ResolveVoiceInput() const;
	UTexture2D* LoadPngTexture(const FString& AbsolutePath, const TCHAR* ObjectName);
	FSlateFontInfo MakePeriodLabelFont(float Size) const;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SpeakLanternTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> WaitLanternTexture;

	TSharedPtr<SOverlay> OverlayWidget;
	TSharedPtr<SImage> LanternImage;
	TSharedPtr<STextBlock> LabelText;
	TSharedPtr<STextBlock> WarningText;
	TSharedPtr<SBorder> BackingBorder;
	TSharedPtr<FSlateColorBrush> BackingBrush;
	FSlateBrush SpeakBrush;
	FSlateBrush WaitBrush;
	bool bOverlayAdded = false;
	bool bLastCanSpeak = false;
	bool bLastSpeakWhileWaitWarning = false;
	bool bHasLastCanSpeak = false;
};
