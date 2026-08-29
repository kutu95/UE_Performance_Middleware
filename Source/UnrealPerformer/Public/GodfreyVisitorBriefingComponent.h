#pragma once

#include "UnrealPerformerApi.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "GodfreyVisitorBriefingComponent.generated.h"

class SBorder;
class SOverlay;
class STextBlock;
class UGodfreyVoiceInputComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGodfreyVisitorBriefingFinishedEvent);

/**
 * Arrival card (R17): dim veil + Constantia copy over SeaIdle, then fade into the scene.
 * Plays before presence Welcome speak. Mic is held so STT cannot steal a turn.
 * Lantern stays above this overlay (higher viewport Z).
 */
UCLASS(ClassGroup = (Godfrey), meta = (BlueprintSpawnableComponent))
class UNREAL_PERFORMER_API UGodfreyVisitorBriefingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGodfreyVisitorBriefingComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintPure, Category = "Godfrey|Vision|Briefing")
	bool IsEnabled() const { return bEnabled; }

	UFUNCTION(BlueprintPure, Category = "Godfrey|Vision|Briefing")
	bool IsPlaying() const { return bPlaying; }

	/** Start the card. False if disabled, already playing, or already shown this occupancy. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Vision|Briefing")
	bool TryPlay();

	/** Occupancy returned to Empty: abort without Welcome, and allow the next visitor to see the card. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Vision|Briefing")
	void NotifyZoneEmpty();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Vision|Briefing")
	void SkipToFinish();

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Vision|Briefing")
	FGodfreyVisitorBriefingFinishedEvent OnBriefingFinished;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Briefing")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Briefing", meta = (MultiLine = "true"))
	FString BodyText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Briefing", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float FadeInSeconds = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Briefing", meta = (ClampMin = "4.0", ClampMax = "40.0"))
	float HoldSeconds = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Briefing", meta = (ClampMin = "0.3", ClampMax = "5.0"))
	float FadeOutSeconds = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Briefing", meta = (ClampMin = "0.4", ClampMax = "0.95"))
	float DimOpacity = 0.78f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Briefing", meta = (ClampMin = "18", ClampMax = "48"))
	float BodyFontSize = 28.f;

	/** Slow rise during hold (pixels). 0 = static card. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Briefing", meta = (ClampMin = "0.0", ClampMax = "120.0"))
	float HoldScrollPixels = 36.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Briefing")
	FKey SkipKey = EKeys::F8;

private:
	void ApplyProjectSettingsDefaults();
	void EnsureOverlay();
	void TearDownOverlay();
	void ApplyVisual(float Alpha, float ScrollPixels);
	void FinishPlaying(bool bBroadcastFinished);
	void ApplyMicHold(bool bHold);
	UGodfreyVoiceInputComponent* ResolveVoiceInput() const;
	FSlateFontInfo MakePeriodBodyFont() const;
	float GetPhaseAlpha() const;
	float GetScrollPixels() const;

	TSharedPtr<SOverlay> OverlayWidget;
	TSharedPtr<SBorder> DimBorder;
	TSharedPtr<STextBlock> BodyBlock;
	TSharedPtr<FSlateColorBrush> DimBrush;
	bool bOverlayAdded = false;
	bool bPlaying = false;
	bool bCompletedThisOccupancy = false;
	float ElapsedSeconds = 0.f;
};
