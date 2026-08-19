#pragma once

#include "UnrealPerformerApi.h"
#include "Components/ActorComponent.h"
#include "GodfreyPerformanceTypes.h"
#include "Styling/SlateBrush.h"
#include "GodfreyVisitorPresenceComponent.generated.h"

class UCanvas;
class UCanvasRenderTarget2D;
class UGodfreyPerformanceStateComponent;
class UGodfreyPerformerAnimationBridgeComponent;
class UMediaPlayer;
class UMediaTexture;
class SOverlay;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FGodfreyVisitorSenseChangedEvent,
	EGodfreyVisitorSenseState,
	NewState,
	EGodfreyVisitorSenseState,
	PreviousState,
	int32,
	EstimatedVisitorCount);

/**
 * First-slice webcam visitor presence:
 * - Opens the first (or name-filtered) webcam via Media Framework
 * - Illumination-normalized empty-scene differencing inside a UV zone
 * - Frame-to-frame motion gate so a moved chair / light patch cannot Welcome
 * - Empty baseline rebase: leave recapture, stillness, periodic refresh
 * - Dwell timers → Empty / Approaching / Present / Leaving
 * - Optional presence-gated Welcome engage (R17) while SeaIdle
 * - Bottom-left debug webcam preview (F9); F10 force occupied; F11 recapture empty
 *
 * Occupancy is appearance+motion, not a person detector.
 * EstimatedVisitorCount is a coarse blob heuristic (0 / 1 / 2+).
 */
UCLASS(ClassGroup = (Godfrey), meta = (BlueprintSpawnableComponent))
class UNREAL_PERFORMER_API UGodfreyVisitorPresenceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGodfreyVisitorPresenceComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintPure, Category = "Godfrey|Vision|Presence")
	EGodfreyVisitorSenseState GetVisitorSenseState() const { return VisitorSenseState; }

	UFUNCTION(BlueprintPure, Category = "Godfrey|Vision|Presence")
	int32 GetEstimatedVisitorCount() const { return EstimatedVisitorCount; }

	UFUNCTION(BlueprintPure, Category = "Godfrey|Vision|Presence")
	bool IsWebcamOpen() const;

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Vision|Presence")
	void SetDebugPreviewVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "Godfrey|Vision|Presence")
	bool IsDebugPreviewVisible() const { return bDebugPreviewVisible; }

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Vision|Presence")
	void RecalibrateEmptyBackground();

	/** Debug: force occupancy as if a visitor is in zone (cleared when set false). */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Vision|Presence")
	void SetForceOccupied(bool bForce);

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Vision|Presence")
	FGodfreyVisitorSenseChangedEvent OnVisitorSenseChanged;

	/** Open webcam on BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision")
	bool bEnableWebcam = true;

	/** Drive Godfrey Welcome engage when Present while SeaIdle (R17). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence")
	bool bEngageOnPresence = true;

	/** After presence Welcome montage is armed, AskGodfrey for a short spoken welcome (UE-owned, R17). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence")
	bool bSpeakWelcomeOnPresence = true;

	/**
	 * Prompt sent to Brain as the presence-welcome turn. Treated as a visitor utterance for session
	 * history; keep it short so Godfrey answers with a brief in-character greeting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (MultiLine = "true"))
	FString WelcomeSpeakPrompt = TEXT(
		"(A visitor has just approached and stands before you. Welcome them warmly in one or two short sentences, in character. Do not wait for them to speak first.)");

	/** Small delay so EngageGreet Welcome montage starts before Thinking/speech is requested. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float WelcomeSpeakDelaySeconds = 0.35f;

	/** When leave dwell completes (Present→…→Empty) while in dialog, spoken goodbye + farewell gesture (R17). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence")
	bool bFarewellOnAbsence = true;

	/**
	 * Prompt for presence-leave goodbye. Brain visitor profile already holds their name when known.
	 * Include [farewell] so R12 latches the wave after the line is spoken.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (MultiLine = "true", EditCondition = "bFarewellOnAbsence"))
	FString FarewellSpeakPrompt = TEXT(
		"(The visitor has walked away and left the scene. Bid them a brief goodbye — use their name if you know it. One short sentence only. End with [farewell].)");

	/** While Present and in dialog, ping NotifyVisitorActivity so silent standing does not farewell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence")
	bool bRefreshActivityWhilePresent = true;

	/**
	 * After empty background is locked, do not absorb the visitor into the baseline (fixes missed leave).
	 * While Present, only unchanged (background) pixels may slow-learn lighting.
	 * Slow empty-only learning remains for window-light drift while sense is Empty.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence")
	bool bFreezeBackgroundWhileVisitorPresent = true;

	/** Substring match against webcam display name; empty = first device. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision")
	FString PreferredDeviceNameFilter;

	/**
	 * After webcam starts, ignore this many seconds (operator is usually still in frame starting PIE).
	 * When the timer ends, capture the empty / absent background and only then run presence.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float StartupIgnoreSeconds = 2.f;

	/** After startup ignore, average this many analysis frames into the empty background before sensing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "1", ClampMax = "30"))
	int32 EmptyBackgroundCaptureFrames = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.2", ClampMax = "10.0"))
	float EnterDwellSeconds = 1.75f;

	/** 0 = farewell as soon as occupancy falls below the leave threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float LeaveDwellSeconds = 2.f;

	/** Normalized UV zone of the visitor mat / stand area (min XY → max XY). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence")
	FVector2D ZoneMinUV = FVector2D(0.12f, 0.18f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence")
	FVector2D ZoneMaxUV = FVector2D(0.88f, 0.96f);

	/** Mean-subtracted luminance |diff| above this counts as changed (0–1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float ChangeThreshold = 0.085f;

	/** Fraction of zone pixels that must change vs empty model to count as occupied (enter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float OccupancyFractionThreshold = 0.06f;

	/**
	 * While Present/Leaving, occupancy must fall below this before Empty (hysteresis).
	 * A still visitor can sit just under the enter threshold without being gone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float OccupancyLeaveFractionThreshold = 0.03f;

	/** Fraction of zone pixels that must change vs the previous frame to count as motion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float MotionFractionThreshold = 0.04f;

	/**
	 * Seconds of motion while Approaching required before Present / Welcome.
	 * A moved chair is a short burst; a walking visitor accumulates more than this during enter dwell.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float MotionConfirmSeconds = 0.8f;

	/**
	 * If Empty but still appearance-occupied with no motion for this long, recapture empty
	 * (chair moved, lights changed). Ignored while Present.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "3.0", ClampMax = "60.0"))
	float StillnessRebaseSeconds = 15.f;

	/** After Present→Empty, wait this long then recapture empty (person has left the frame). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float LeaveRecaptureDelaySeconds = 0.75f;

	/** While Empty and unoccupied, recapture empty on this interval (daylight). 0 = off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.0", ClampMax = "900.0"))
	float PeriodicEmptyRefreshSeconds = 180.f;

	/** Background learn rate while empty / stable and unoccupied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float BackgroundLearnRateEmpty = 0.04f;

	/** Learn while occupied — keep 0 so the visitor is never baked into the empty baseline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float BackgroundLearnRateOccupied = 0.f;

	/** While Present, slow-learn pixels that match the empty model (lighting on walls/floor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float BackgroundLearnRatePresentUnchanged = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Presence", meta = (ClampMin = "0.25", ClampMax = "10.0"))
	float AnalysisIntervalSeconds = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Debug")
	bool bShowDebugPreview = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Debug")
	FKey DebugPreviewToggleKey = EKeys::F9;

	/** Toggle to force occupied (debug). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Debug")
	FKey ForceOccupiedToggleKey = EKeys::F10;

	/** Recapture the empty-room background now (operator / debug). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Debug")
	FKey RecaptureEmptyToggleKey = EKeys::F11;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Debug", meta = (ClampMin = "80", ClampMax = "640"))
	float DebugPreviewWidth = 320.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Debug", meta = (ClampMin = "60", ClampMax = "480"))
	float DebugPreviewHeight = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision|Debug", meta = (ClampMin = "0", ClampMax = "64"))
	float DebugPreviewMargin = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Vision")
	FName CharacterActorTag = FName(TEXT("GodfreyCharacter"));

protected:
	UFUNCTION()
	void HandleMediaOpened(FString OpenedUrl);

	UFUNCTION()
	void HandleMediaOpenFailed(FString FailedUrl);

	UFUNCTION()
	void HandleAnalysisCanvasUpdate(UCanvas* Canvas, int32 Width, int32 Height);

	/** Delayed: webcams often report tracks incorrectly on the open callback frame. */
	UFUNCTION()
	void ConfigureAndPlayWebcam();

private:
	void ApplyProjectSettingsDefaults();
	void OpenWebcam();
	void CloseWebcam();
	/** Summary at Log; per-format dump only at Verbose (WMF webcams can expose 200+ formats). */
	void LogVideoTracks(const TCHAR* Context) const;
	bool SelectBestVideoTrackAndFormat();
	void EnsureAnalysisTarget();
	void TickAnalysis(float DeltaTime);
	void AnalyzeFrame();
	void UpdateSenseState(float DeltaTime);
	void BeginEmptyBackgroundCapture(const TCHAR* Reason, bool bResetSenseToEmpty);
	void TickEmptyBaselineMaintenance(float DeltaTime);
	float GetEmptyBackgroundAgeSeconds() const;
	void SetSenseState(EGodfreyVisitorSenseState NewState);
	void TryPresenceEngage();
	void RequestWelcomeSpeak();
	UFUNCTION()
	void HandleWelcomeSpeakTimer();
	void TryPresenceFarewell();
	void RequestFarewellSpeak();
	UFUNCTION()
	void HandleFarewellSpeakTimer();
	void TickPendingFarewellSpeak(float DeltaTime);
	void TickActivityRefresh(float DeltaTime);
	void TickOccupancyDebugLog(float DeltaTime);
	void ResolvePerformerComponents();
	AActor* ResolveGodfreyActor() const;
	class UGodfreyDirectSpeechComponent* ResolveDirectSpeech() const;

	void EnsureDebugPreview();
	void TearDownDebugPreview();
	void UpdateDebugPreviewLayout();
	void DrawDebugStatusText() const;

	static float Luminance01(const FColor& C);
	int32 EstimateVisitorCountFromMask(const TArray<uint8>& Mask, int32 ZoneW, int32 ZoneH) const;

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> MediaPlayer;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> MediaTexture;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasRenderTarget2D> AnalysisTarget;

	UPROPERTY(Transient)
	TObjectPtr<UGodfreyPerformanceStateComponent> PerformerState;

	UPROPERTY(Transient)
	TObjectPtr<UGodfreyPerformerAnimationBridgeComponent> AnimationBridge;

	EGodfreyVisitorSenseState VisitorSenseState = EGodfreyVisitorSenseState::Empty;
	int32 EstimatedVisitorCount = 0;
	bool bRawOccupied = false;
	bool bRawMotion = false;
	bool bForceOccupied = false;
	bool bBackgroundReady = false;
	bool bDebugPreviewVisible = false;
	bool bMediaOpenPending = false;
	bool bWebcamConfigured = false;
	bool bStartupIgnoreActive = false;
	bool bCapturingEmptyBackground = false;
	bool bPresenceFarewellRequested = false;
	bool bPendingFarewellSpeak = false;
	int32 WebcamPlayRetryCount = 0;
	int32 EmptyBackgroundFramesCaptured = 0;
	float WebcamPlayRetryCountdown = 0.f;
	float StartupIgnoreRemaining = 0.f;
	float AnalysisCountdown = 0.f;
	float OccupancyDebugLogCountdown = 0.f;
	float PendingFarewellSpeakCountdown = 0.f;
	FTimerHandle WebcamConfigureTimerHandle;
	FTimerHandle WelcomeSpeakTimerHandle;
	FTimerHandle FarewellSpeakTimerHandle;
	float EnterDwellRemaining = -1.f;
	float LeaveDwellRemaining = -1.f;
	float ActivityRefreshCountdown = 0.f;
	float OccupancyFraction = 0.f;
	float MotionFraction = 0.f;
	float MotionConfirmAccumulatedSeconds = 0.f;
	float StillnessRebaseAccumulatedSeconds = 0.f;
	float LeaveRecaptureCountdown = -1.f;
	float PeriodicEmptyRefreshCountdown = -1.f;
	float EmptyBackgroundLockedWorldTime = 0.f;
	FString ActiveDeviceDisplayName;
	FString ActiveDeviceUrl;

	TArray<float> BackgroundLuma;
	TArray<float> PreviousFrameLuma;
	int32 AnalysisWidth = 160;
	int32 AnalysisHeight = 90;

	TSharedPtr<SOverlay> DebugOverlayWidget;
	TSharedPtr<class SImage> DebugImageWidget;
	TSharedPtr<class SBorder> DebugZoneBorder;
	TSharedPtr<class STextBlock> DebugStatusText;
	FSlateBrush DebugMediaBrush;
	bool bDebugOverlayAdded = false;
};
