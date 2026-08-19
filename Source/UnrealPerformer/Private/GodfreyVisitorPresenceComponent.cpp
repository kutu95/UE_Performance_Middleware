#include "GodfreyVisitorPresenceComponent.h"

#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "GodfreyDiagnostics.h"
#include "GodfreyDirectSpeechComponent.h"
#include "GodfreyPerformanceStateComponent.h"
#include "GodfreyPerformerAnimationBridgeComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Misc/MediaBlueprintFunctionLibrary.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "UnrealPerformerGodfreySettings.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace GodfreyVisitorPresencePrivate
{
static const TCHAR* SenseStateName(EGodfreyVisitorSenseState State)
{
	switch (State)
	{
	case EGodfreyVisitorSenseState::Empty: return TEXT("Empty");
	case EGodfreyVisitorSenseState::Approaching: return TEXT("Approaching");
	case EGodfreyVisitorSenseState::Present: return TEXT("Present");
	case EGodfreyVisitorSenseState::Leaving: return TEXT("Leaving");
	default: return TEXT("?");
	}
}
} // namespace

UGodfreyVisitorPresenceComponent::UGodfreyVisitorPresenceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UGodfreyVisitorPresenceComponent::BeginPlay()
{
	Super::BeginPlay();
	ApplyProjectSettingsDefaults();

#if UE_BUILD_SHIPPING
	bShowDebugPreview = false;
	bDebugPreviewVisible = false;
#else
	bDebugPreviewVisible = bShowDebugPreview;
#endif

	ResolvePerformerComponents();

	if (bEnableWebcam)
	{
		OpenWebcam();
	}

	EnsureDebugPreview();
	UE_LOG(LogGodfreyVision, Log,
		TEXT("VisitorPresence: BeginPlay enableWebcam=%d engageOnPresence=%d preview=%d"),
		bEnableWebcam ? 1 : 0,
		bEngageOnPresence ? 1 : 0,
		bDebugPreviewVisible ? 1 : 0);
}

void UGodfreyVisitorPresenceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WelcomeSpeakTimerHandle);
		World->GetTimerManager().ClearTimer(FarewellSpeakTimerHandle);
	}
	TearDownDebugPreview();
	CloseWebcam();
	Super::EndPlay(EndPlayReason);
}

void UGodfreyVisitorPresenceComponent::ApplyProjectSettingsDefaults()
{
	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	if (!Settings)
	{
		return;
	}

	bEnableWebcam = Settings->bGodfreyEnableVisitorPresenceWebcam;
	bEngageOnPresence = Settings->bGodfreyPresenceEngageOnDwell;
	bSpeakWelcomeOnPresence = Settings->bGodfreyPresenceWelcomeSpeak;
	if (!Settings->GodfreyPresenceWelcomeSpeakPrompt.IsEmpty())
	{
		WelcomeSpeakPrompt = Settings->GodfreyPresenceWelcomeSpeakPrompt;
	}
	bFarewellOnAbsence = Settings->bGodfreyPresenceFarewellOnAbsence;
	if (!Settings->GodfreyPresenceFarewellSpeakPrompt.IsEmpty())
	{
		FarewellSpeakPrompt = Settings->GodfreyPresenceFarewellSpeakPrompt;
	}
	bShowDebugPreview = Settings->bGodfreyShowWebcamDebugPreview;
	DebugPreviewToggleKey = Settings->GodfreyWebcamPreviewToggleKey;
	ForceOccupiedToggleKey = Settings->GodfreyWebcamForceOccupiedToggleKey;
	RecaptureEmptyToggleKey = Settings->GodfreyWebcamRecaptureEmptyKey;
	PreferredDeviceNameFilter = Settings->GodfreyWebcamDeviceNameFilter;
	StartupIgnoreSeconds = Settings->GodfreyWebcamStartupIgnoreSeconds;
	EnterDwellSeconds = Settings->GodfreyWebcamEnterDwellSeconds;
	LeaveDwellSeconds = Settings->GodfreyWebcamLeaveDwellSeconds;
	OccupancyLeaveFractionThreshold = Settings->GodfreyWebcamOccupancyLeaveFractionThreshold;
}

void UGodfreyVisitorPresenceComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if !UE_BUILD_SHIPPING
	if (!AnimationBridge)
	{
		ResolvePerformerComponents();
	}
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (PC->WasInputKeyJustPressed(DebugPreviewToggleKey))
		{
			SetDebugPreviewVisible(!bDebugPreviewVisible);
		}
		if (PC->WasInputKeyJustPressed(ForceOccupiedToggleKey))
		{
			SetForceOccupied(!bForceOccupied);
		}
		if (PC->WasInputKeyJustPressed(RecaptureEmptyToggleKey))
		{
			UE_LOG(LogGodfreyVision, Log, TEXT("VisitorPresence: operator recapture empty background."));
			RecalibrateEmptyBackground();
		}
		// Same PC poll as F9/F10. Bridge tick can miss K if the serialized FKey is None.
		if (AnimationBridge && AnimationBridge->bEnableDebugPerformancePlayKey)
		{
			const FKey CaptureKey = AnimationBridge->DebugPerformancePlayKey.IsValid()
				? AnimationBridge->DebugPerformancePlayKey
				: EKeys::K;
			if (PC->WasInputKeyJustPressed(CaptureKey))
			{
				UE_LOG(LogGodfreyVision, Log,
					TEXT("VisitorPresence: operator capture key '%s' — forwarding to animation bridge."),
					*CaptureKey.ToString());
				AnimationBridge->PlayOperatorPerformanceClip(AnimationBridge->DebugPerformancePlayCueId);
			}
		}
	}
#endif

	if (!PerformerState || !AnimationBridge)
	{
		ResolvePerformerComponents();
	}

	TickAnalysis(DeltaTime);
	TickPendingFarewellSpeak(DeltaTime);
	TickActivityRefresh(DeltaTime);
	TickOccupancyDebugLog(DeltaTime);
	UpdateDebugPreviewLayout();
	DrawDebugStatusText();
}

bool UGodfreyVisitorPresenceComponent::IsWebcamOpen() const
{
	return MediaPlayer && (MediaPlayer->IsPlaying() || MediaPlayer->IsPreparing() || MediaPlayer->IsReady());
}

void UGodfreyVisitorPresenceComponent::SetDebugPreviewVisible(bool bVisible)
{
	bDebugPreviewVisible = bVisible;
	if (bDebugPreviewVisible)
	{
		EnsureDebugPreview();
	}
	else
	{
		TearDownDebugPreview();
	}
}

void UGodfreyVisitorPresenceComponent::RecalibrateEmptyBackground()
{
	BeginEmptyBackgroundCapture(TEXT("operator"), true);
}

void UGodfreyVisitorPresenceComponent::BeginEmptyBackgroundCapture(const TCHAR* Reason, bool bResetSenseToEmpty)
{
	BackgroundLuma.Reset();
	PreviousFrameLuma.Reset();
	bBackgroundReady = false;
	bCapturingEmptyBackground = true;
	EmptyBackgroundFramesCaptured = 0;
	bRawOccupied = false;
	bRawMotion = false;
	EstimatedVisitorCount = 0;
	OccupancyFraction = 0.f;
	MotionFraction = 0.f;
	MotionConfirmAccumulatedSeconds = 0.f;
	StillnessRebaseAccumulatedSeconds = 0.f;
	PeriodicEmptyRefreshCountdown = -1.f;
	EnterDwellRemaining = -1.f;
	LeaveDwellRemaining = -1.f;
	if (bResetSenseToEmpty && VisitorSenseState != EGodfreyVisitorSenseState::Empty)
	{
		SetSenseState(EGodfreyVisitorSenseState::Empty);
	}
	// Capture owns the baseline; do not also schedule a leave recapture.
	LeaveRecaptureCountdown = -1.f;
	UE_LOG(LogGodfreyVision, Log,
		TEXT("VisitorPresence: empty background capture started (%s, will average %d frames)."),
		Reason ? Reason : TEXT("?"),
		FMath::Max(1, EmptyBackgroundCaptureFrames));
}

float UGodfreyVisitorPresenceComponent::GetEmptyBackgroundAgeSeconds() const
{
	if (EmptyBackgroundLockedWorldTime <= 0.f)
	{
		return -1.f;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return -1.f;
	}
	return FMath::Max(0.f, World->GetTimeSeconds() - EmptyBackgroundLockedWorldTime);
}

void UGodfreyVisitorPresenceComponent::SetForceOccupied(bool bForce)
{
	bForceOccupied = bForce;
	UE_LOG(LogGodfreyVision, Log, TEXT("VisitorPresence: forceOccupied=%d"), bForceOccupied ? 1 : 0);
}

void UGodfreyVisitorPresenceComponent::OpenWebcam()
{
	if (!MediaPlayer)
	{
		MediaPlayer = NewObject<UMediaPlayer>(this, TEXT("GodfreyWebcamPlayer"));
		MediaPlayer->PlayOnOpen = true;
		MediaPlayer->OnMediaOpened.AddDynamic(this, &UGodfreyVisitorPresenceComponent::HandleMediaOpened);
		MediaPlayer->OnMediaOpenFailed.AddDynamic(this, &UGodfreyVisitorPresenceComponent::HandleMediaOpenFailed);
	}
	if (!MediaTexture)
	{
		MediaTexture = NewObject<UMediaTexture>(this, TEXT("GodfreyWebcamTexture"));
		// Classic output is more reliable for Slate SImage; NewStyleOutput often stays black.
		MediaTexture->NewStyleOutput = false;
		MediaTexture->AutoClear = true;
		MediaTexture->EnableGenMips = false;
		MediaTexture->AddressX = TA_Clamp;
		MediaTexture->AddressY = TA_Clamp;
		MediaTexture->SetMediaPlayer(MediaPlayer);
		MediaTexture->UpdateResource();
	}

	TArray<FMediaCaptureDevice> Devices;
	UMediaBlueprintFunctionLibrary::EnumerateWebcamCaptureDevices(Devices, -1);
	if (Devices.Num() == 0)
	{
		UMediaBlueprintFunctionLibrary::EnumerateVideoCaptureDevices(Devices, -1);
	}

	if (Devices.Num() == 0)
	{
		UE_LOG(LogGodfreyVision, Error, TEXT("VisitorPresence: no webcam / video capture devices found."));
		return;
	}

	const FMediaCaptureDevice* Chosen = &Devices[0];
	if (!PreferredDeviceNameFilter.IsEmpty())
	{
		for (const FMediaCaptureDevice& Device : Devices)
		{
			if (Device.DisplayName.ToString().Contains(PreferredDeviceNameFilter, ESearchCase::IgnoreCase))
			{
				Chosen = &Device;
				break;
			}
		}
	}

	ActiveDeviceDisplayName = Chosen->DisplayName.ToString();
	ActiveDeviceUrl = Chosen->Url;
	bMediaOpenPending = true;

	UE_LOG(LogGodfreyVision, Log,
		TEXT("VisitorPresence: opening webcam '%s' url=%s (%d devices)"),
		*ActiveDeviceDisplayName,
		*ActiveDeviceUrl,
		Devices.Num());

	if (!MediaPlayer->OpenUrl(ActiveDeviceUrl))
	{
		bMediaOpenPending = false;
		UE_LOG(LogGodfreyVision, Error, TEXT("VisitorPresence: OpenUrl failed for '%s'."), *ActiveDeviceDisplayName);
	}

	DebugMediaBrush = FSlateBrush();
	DebugMediaBrush.SetResourceObject(MediaTexture);
	DebugMediaBrush.ImageSize = FVector2D(DebugPreviewWidth, DebugPreviewHeight);
	DebugMediaBrush.DrawAs = ESlateBrushDrawType::Image;
}

void UGodfreyVisitorPresenceComponent::CloseWebcam()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WebcamConfigureTimerHandle);
		World->GetTimerManager().ClearTimer(WelcomeSpeakTimerHandle);
		World->GetTimerManager().ClearTimer(FarewellSpeakTimerHandle);
	}
	if (MediaPlayer)
	{
		MediaPlayer->OnMediaOpened.RemoveDynamic(this, &UGodfreyVisitorPresenceComponent::HandleMediaOpened);
		MediaPlayer->OnMediaOpenFailed.RemoveDynamic(this, &UGodfreyVisitorPresenceComponent::HandleMediaOpenFailed);
		MediaPlayer->Close();
	}
	MediaPlayer = nullptr;
	MediaTexture = nullptr;
	AnalysisTarget = nullptr;
	bMediaOpenPending = false;
	bWebcamConfigured = false;
	WebcamPlayRetryCount = 0;
}

void UGodfreyVisitorPresenceComponent::HandleMediaOpened(FString OpenedUrl)
{
	bMediaOpenPending = false;
	bWebcamConfigured = false;
	WebcamPlayRetryCount = 0;
	WebcamPlayRetryCountdown = 0.f;

	UE_LOG(LogGodfreyVision, Log, TEXT("VisitorPresence: media opened url=%s — delaying track/format select"), *OpenedUrl);
	LogVideoTracks(TEXT("on-open"));

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WebcamConfigureTimerHandle);
		// WMF webcams commonly expose a black track until tracks settle a beat after open.
		World->GetTimerManager().SetTimer(
			WebcamConfigureTimerHandle,
			this,
			&UGodfreyVisitorPresenceComponent::ConfigureAndPlayWebcam,
			0.35f,
			false);
	}
	else
	{
		ConfigureAndPlayWebcam();
	}
}

void UGodfreyVisitorPresenceComponent::LogVideoTracks(const TCHAR* Context) const
{
	if (!MediaPlayer)
	{
		return;
	}

	const int32 NumTracks = MediaPlayer->GetNumTracks(EMediaPlayerTrack::Video);
	int32 TotalFormats = 0;
	for (int32 Track = 0; Track < NumTracks; ++Track)
	{
		TotalFormats += MediaPlayer->GetNumTrackFormats(EMediaPlayerTrack::Video, Track);
	}

	UE_LOG(LogGodfreyVision, Log,
		TEXT("VisitorPresence: tracks (%s) videoTracks=%d selected=%d formats=%d (per-format dump is Verbose)"),
		Context,
		NumTracks,
		MediaPlayer->GetSelectedTrack(EMediaPlayerTrack::Video),
		TotalFormats);

	if (!UE_LOG_ACTIVE(LogGodfreyVision, Verbose))
	{
		return;
	}

	for (int32 Track = 0; Track < NumTracks; ++Track)
	{
		const int32 NumFormats = MediaPlayer->GetNumTrackFormats(EMediaPlayerTrack::Video, Track);
		const FText DisplayName = MediaPlayer->GetTrackDisplayName(EMediaPlayerTrack::Video, Track);
		UE_LOG(LogGodfreyVision, Verbose,
			TEXT("VisitorPresence:   track %d '%s' formats=%d"),
			Track,
			*DisplayName.ToString(),
			NumFormats);
		for (int32 Format = 0; Format < NumFormats; ++Format)
		{
			const FIntPoint Dim = MediaPlayer->GetVideoTrackDimensions(Track, Format);
			const float Fps = MediaPlayer->GetVideoTrackFrameRate(Track, Format);
			UE_LOG(LogGodfreyVision, Verbose,
				TEXT("VisitorPresence:     format %d %dx%d @ %.2ffps"),
				Format,
				Dim.X,
				Dim.Y,
				Fps);
		}
	}
}

bool UGodfreyVisitorPresenceComponent::SelectBestVideoTrackAndFormat()
{
	if (!MediaPlayer)
	{
		return false;
	}

	const int32 NumTracks = MediaPlayer->GetNumTracks(EMediaPlayerTrack::Video);
	if (NumTracks <= 0)
	{
		UE_LOG(LogGodfreyVision, Warning, TEXT("VisitorPresence: no video tracks yet."));
		return false;
	}

	// Prefer track 0 — on many WMF webcams track 1 is a black/dummy stream.
	int32 BestTrack = INDEX_NONE;
	int32 BestFormat = INDEX_NONE;
	int32 BestScore = MIN_int32;

	auto Consider = [&](int32 Track)
	{
		const int32 NumFormats = MediaPlayer->GetNumTrackFormats(EMediaPlayerTrack::Video, Track);
		for (int32 Format = 0; Format < NumFormats; ++Format)
		{
			const FIntPoint Dim = MediaPlayer->GetVideoTrackDimensions(Track, Format);
			if (Dim.X < 16 || Dim.Y < 16)
			{
				continue;
			}
			const float Fps = MediaPlayer->GetVideoTrackFrameRate(Track, Format);
			// Prefer ~640x480 / 30fps; penalize tiny or huge formats slightly.
			const int32 Area = Dim.X * Dim.Y;
			int32 Score = 1000000 - FMath::Abs(Area - (640 * 480));
			Score -= FMath::RoundToInt(FMath::Abs(Fps - 30.f) * 100.f);
			// Strong preference for track 0.
			if (Track == 0)
			{
				Score += 500000;
			}
			if (Score > BestScore)
			{
				BestScore = Score;
				BestTrack = Track;
				BestFormat = Format;
			}
		}
	};

	Consider(0);
	for (int32 Track = 1; Track < NumTracks; ++Track)
	{
		Consider(Track);
	}

	if (BestTrack == INDEX_NONE)
	{
		// Fallback: force track 0 / format 0 even if dimensions were not reported yet.
		BestTrack = 0;
		BestFormat = 0;
	}

	const bool bTrackOk = MediaPlayer->SelectTrack(EMediaPlayerTrack::Video, BestTrack);
	bool bFormatOk = true;
	if (MediaPlayer->GetNumTrackFormats(EMediaPlayerTrack::Video, BestTrack) > 0)
	{
		bFormatOk = MediaPlayer->SetTrackFormat(EMediaPlayerTrack::Video, BestTrack, BestFormat);
	}

	const FIntPoint Dim = MediaPlayer->GetVideoTrackDimensions(BestTrack, BestFormat);
	UE_LOG(LogGodfreyVision, Log,
		TEXT("VisitorPresence: selected track=%d format=%d (%dx%d) trackOk=%d formatOk=%d"),
		BestTrack,
		BestFormat,
		Dim.X,
		Dim.Y,
		bTrackOk ? 1 : 0,
		bFormatOk ? 1 : 0);
	return bTrackOk;
}

void UGodfreyVisitorPresenceComponent::ConfigureAndPlayWebcam()
{
	if (!MediaPlayer)
	{
		return;
	}

	LogVideoTracks(TEXT("configure"));
	SelectBestVideoTrackAndFormat();

	if (MediaTexture)
	{
		MediaTexture->NewStyleOutput = false;
		MediaTexture->SetMediaPlayer(MediaPlayer);
		MediaTexture->UpdateResource();
	}

	MediaPlayer->SetLooping(false);
	MediaPlayer->SetRate(1.0f);
	const bool bPlayOk = MediaPlayer->Play();
	bWebcamConfigured = true;
	WebcamPlayRetryCount = 0;
	WebcamPlayRetryCountdown = 0.25f;

	EnsureAnalysisTarget();
	// Do not bake background yet — operator is usually still in frame at PIE start.
	BackgroundLuma.Reset();
	bBackgroundReady = false;
	bCapturingEmptyBackground = false;
	EmptyBackgroundFramesCaptured = 0;
	StartupIgnoreRemaining = FMath::Max(0.f, StartupIgnoreSeconds);
	bStartupIgnoreActive = StartupIgnoreRemaining > KINDA_SMALL_NUMBER;
	if (!bStartupIgnoreActive)
	{
		BeginEmptyBackgroundCapture(TEXT("startup"), true);
	}
	else
	{
		UE_LOG(LogGodfreyVision, Log,
			TEXT("VisitorPresence: startup ignore %.2fs — step out of frame, then empty background will be captured."),
			StartupIgnoreRemaining);
	}
	EnsureDebugPreview();

	if (DebugImageWidget.IsValid())
	{
		DebugMediaBrush.SetResourceObject(MediaTexture);
		DebugMediaBrush.ImageSize = FVector2D(DebugPreviewWidth, DebugPreviewHeight);
		DebugImageWidget->SetImage(&DebugMediaBrush);
	}

	UE_LOG(LogGodfreyVision, Log,
		TEXT("VisitorPresence: configure complete playOk=%d playing=%d ready=%d tex=%dx%d startupIgnore=%.2fs"),
		bPlayOk ? 1 : 0,
		MediaPlayer->IsPlaying() ? 1 : 0,
		MediaPlayer->IsReady() ? 1 : 0,
		MediaTexture ? MediaTexture->GetWidth() : 0,
		MediaTexture ? MediaTexture->GetHeight() : 0,
		StartupIgnoreRemaining);
}

void UGodfreyVisitorPresenceComponent::HandleMediaOpenFailed(FString FailedUrl)
{
	bMediaOpenPending = false;
	UE_LOG(LogGodfreyVision, Error, TEXT("VisitorPresence: media open failed url=%s"), *FailedUrl);
}

void UGodfreyVisitorPresenceComponent::EnsureAnalysisTarget()
{
	if (AnalysisTarget)
	{
		return;
	}

	AnalysisTarget = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(
		this,
		UCanvasRenderTarget2D::StaticClass(),
		AnalysisWidth,
		AnalysisHeight);
	if (!AnalysisTarget)
	{
		UE_LOG(LogGodfreyVision, Error, TEXT("VisitorPresence: failed to create analysis render target."));
		return;
	}

	AnalysisTarget->OnCanvasRenderTargetUpdate.AddDynamic(
		this,
		&UGodfreyVisitorPresenceComponent::HandleAnalysisCanvasUpdate);
}

void UGodfreyVisitorPresenceComponent::HandleAnalysisCanvasUpdate(UCanvas* Canvas, int32 Width, int32 Height)
{
	if (!Canvas || !MediaTexture)
	{
		return;
	}

	Canvas->K2_DrawTexture(
		MediaTexture,
		FVector2D::ZeroVector,
		FVector2D(static_cast<float>(Width), static_cast<float>(Height)),
		FVector2D::ZeroVector,
		FVector2D::UnitVector,
		FLinearColor::White,
		BLEND_Opaque);
}

void UGodfreyVisitorPresenceComponent::TickAnalysis(float DeltaTime)
{
	// Webcam Play() often fails on the open frame — keep retrying briefly.
	if (MediaPlayer && bWebcamConfigured && !MediaPlayer->IsPlaying() && WebcamPlayRetryCount < 30)
	{
		WebcamPlayRetryCountdown -= DeltaTime;
		if (WebcamPlayRetryCountdown <= 0.f)
		{
			WebcamPlayRetryCountdown = 0.25f;
			++WebcamPlayRetryCount;
			SelectBestVideoTrackAndFormat();
			const bool bPlayOk = MediaPlayer->Play();
			UE_LOG(LogGodfreyVision, Verbose,
				TEXT("VisitorPresence: play retry #%d ok=%d preparing=%d ready=%d"),
				WebcamPlayRetryCount,
				bPlayOk ? 1 : 0,
				MediaPlayer->IsPreparing() ? 1 : 0,
				MediaPlayer->IsReady() ? 1 : 0);
		}
	}

	if (!MediaPlayer || !MediaTexture || !MediaPlayer->IsPlaying())
	{
		if (bForceOccupied && !bStartupIgnoreActive && !bCapturingEmptyBackground)
		{
			UpdateSenseState(DeltaTime);
		}
		return;
	}

	if (bStartupIgnoreActive)
	{
		StartupIgnoreRemaining -= DeltaTime;
		bRawOccupied = false;
		bRawMotion = false;
		EstimatedVisitorCount = 0;
		OccupancyFraction = 0.f;
		MotionFraction = 0.f;
		if (StartupIgnoreRemaining > 0.f)
		{
			return;
		}
		bStartupIgnoreActive = false;
		StartupIgnoreRemaining = 0.f;
		UE_LOG(LogGodfreyVision, Log,
			TEXT("VisitorPresence: startup ignore ended — capturing absent/empty background now."));
		BeginEmptyBackgroundCapture(TEXT("startup"), true);
	}

	TickEmptyBaselineMaintenance(DeltaTime);

	EnsureAnalysisTarget();
	AnalysisCountdown -= DeltaTime;
	if (AnalysisCountdown > 0.f)
	{
		if (!bCapturingEmptyBackground)
		{
			UpdateSenseState(DeltaTime);
		}
		return;
	}
	AnalysisCountdown = AnalysisIntervalSeconds;
	AnalyzeFrame();
	if (!bCapturingEmptyBackground)
	{
		UpdateSenseState(DeltaTime);
	}
}

void UGodfreyVisitorPresenceComponent::AnalyzeFrame()
{
	if (!AnalysisTarget || !GetWorld())
	{
		return;
	}

	AnalysisTarget->UpdateResource();

	TArray<FColor> Samples;
	if (!UKismetRenderingLibrary::ReadRenderTarget(this, AnalysisTarget, Samples, false)
		|| Samples.Num() < AnalysisWidth * AnalysisHeight)
	{
		return;
	}

	const int32 X0 = FMath::Clamp(FMath::FloorToInt(ZoneMinUV.X * AnalysisWidth), 0, AnalysisWidth - 1);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(ZoneMinUV.Y * AnalysisHeight), 0, AnalysisHeight - 1);
	const int32 X1 = FMath::Clamp(FMath::CeilToInt(ZoneMaxUV.X * AnalysisWidth), X0 + 1, AnalysisWidth);
	const int32 Y1 = FMath::Clamp(FMath::CeilToInt(ZoneMaxUV.Y * AnalysisHeight), Y0 + 1, AnalysisHeight);
	const int32 ZoneW = X1 - X0;
	const int32 ZoneH = Y1 - Y0;
	const int32 ZoneCount = ZoneW * ZoneH;
	if (ZoneCount <= 0)
	{
		return;
	}

	TArray<float> FrameLuma;
	FrameLuma.SetNumUninitialized(ZoneCount);
	double Sum = 0.0;
	int32 Write = 0;
	for (int32 Y = Y0; Y < Y1; ++Y)
	{
		for (int32 X = X0; X < X1; ++X)
		{
			const float L = Luminance01(Samples[Y * AnalysisWidth + X]);
			FrameLuma[Write++] = L;
			Sum += L;
		}
	}
	const float FrameMean = static_cast<float>(Sum / ZoneCount);

	if (bCapturingEmptyBackground || BackgroundLuma.Num() != ZoneCount)
	{
		const int32 Needed = FMath::Max(1, EmptyBackgroundCaptureFrames);
		if (BackgroundLuma.Num() != ZoneCount)
		{
			BackgroundLuma = FrameLuma;
			EmptyBackgroundFramesCaptured = 1;
		}
		else
		{
			++EmptyBackgroundFramesCaptured;
			const float Alpha = 1.f / static_cast<float>(EmptyBackgroundFramesCaptured);
			for (int32 i = 0; i < ZoneCount; ++i)
			{
				BackgroundLuma[i] = FMath::Lerp(BackgroundLuma[i], FrameLuma[i], Alpha);
			}
		}

		bRawOccupied = false;
		bRawMotion = false;
		EstimatedVisitorCount = 0;
		OccupancyFraction = 0.f;
		MotionFraction = 0.f;
		bBackgroundReady = false;

		if (EmptyBackgroundFramesCaptured >= Needed)
		{
			bCapturingEmptyBackground = false;
			bBackgroundReady = true;
			EmptyBackgroundLockedWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
			PeriodicEmptyRefreshCountdown = PeriodicEmptyRefreshSeconds;
			PreviousFrameLuma.Reset();
			UE_LOG(LogGodfreyVision, Log,
				TEXT("VisitorPresence: empty/absent background locked (%d frames averaged, reason complete). Presence sensing active."),
				EmptyBackgroundFramesCaptured);
		}
		return;
	}

	double BgSum = 0.0;
	for (float V : BackgroundLuma)
	{
		BgSum += V;
	}
	const float BgMean = static_cast<float>(BgSum / ZoneCount);

	const bool bHavePrev = PreviousFrameLuma.Num() == ZoneCount;
	double PrevSum = 0.0;
	if (bHavePrev)
	{
		for (float V : PreviousFrameLuma)
		{
			PrevSum += V;
		}
	}
	const float PrevMean = bHavePrev ? static_cast<float>(PrevSum / ZoneCount) : 0.f;

	TArray<uint8> Mask;
	Mask.SetNumZeroed(ZoneCount);
	int32 Changed = 0;
	int32 MotionChanged = 0;
	for (int32 i = 0; i < ZoneCount; ++i)
	{
		const float FrameNorm = FrameLuma[i] - FrameMean;
		const float BgNorm = BackgroundLuma[i] - BgMean;
		const float Diff = FMath::Abs(FrameNorm - BgNorm);
		if (Diff >= ChangeThreshold)
		{
			Mask[i] = 1;
			++Changed;
		}
		if (bHavePrev)
		{
			const float PrevNorm = PreviousFrameLuma[i] - PrevMean;
			if (FMath::Abs(FrameNorm - PrevNorm) >= ChangeThreshold)
			{
				++MotionChanged;
			}
		}
	}

	OccupancyFraction = static_cast<float>(Changed) / static_cast<float>(ZoneCount);
	MotionFraction = bHavePrev ? static_cast<float>(MotionChanged) / static_cast<float>(ZoneCount) : 0.f;
	const bool bUseLeaveHysteresis =
		VisitorSenseState == EGodfreyVisitorSenseState::Present
		|| VisitorSenseState == EGodfreyVisitorSenseState::Leaving;
	const float OccupiedNeed = bUseLeaveHysteresis
		? OccupancyLeaveFractionThreshold
		: OccupancyFractionThreshold;
	bRawOccupied = OccupancyFraction >= OccupiedNeed;
	bRawMotion = MotionFraction >= MotionFractionThreshold;
	EstimatedVisitorCount = bRawOccupied ? EstimateVisitorCountFromMask(Mask, ZoneW, ZoneH) : 0;
	PreviousFrameLuma = FrameLuma;

	const bool bSensePresent = VisitorSenseState == EGodfreyVisitorSenseState::Present
		|| VisitorSenseState == EGodfreyVisitorSenseState::Leaving;
	const bool bSenseEmpty = VisitorSenseState == EGodfreyVisitorSenseState::Empty;

	if (bSenseEmpty && !bRawOccupied && BackgroundLearnRateEmpty > KINDA_SMALL_NUMBER)
	{
		for (int32 i = 0; i < ZoneCount; ++i)
		{
			BackgroundLuma[i] = FMath::Lerp(BackgroundLuma[i], FrameLuma[i], BackgroundLearnRateEmpty);
		}
	}
	else if (bSensePresent && bFreezeBackgroundWhileVisitorPresent
		&& BackgroundLearnRatePresentUnchanged > KINDA_SMALL_NUMBER)
	{
		for (int32 i = 0; i < ZoneCount; ++i)
		{
			if (Mask[i] == 0)
			{
				BackgroundLuma[i] = FMath::Lerp(
					BackgroundLuma[i],
					FrameLuma[i],
					BackgroundLearnRatePresentUnchanged);
			}
		}
	}
	else if (!bFreezeBackgroundWhileVisitorPresent)
	{
		const float Learn = bRawOccupied ? BackgroundLearnRateOccupied : BackgroundLearnRateEmpty;
		if (Learn > KINDA_SMALL_NUMBER)
		{
			for (int32 i = 0; i < ZoneCount; ++i)
			{
				BackgroundLuma[i] = FMath::Lerp(BackgroundLuma[i], FrameLuma[i], Learn);
			}
		}
	}
	bBackgroundReady = true;
}

int32 UGodfreyVisitorPresenceComponent::EstimateVisitorCountFromMask(
	const TArray<uint8>& Mask,
	int32 ZoneW,
	int32 ZoneH) const
{
	if (ZoneW <= 0 || ZoneH <= 0 || Mask.Num() != ZoneW * ZoneH)
	{
		return bRawOccupied ? 1 : 0;
	}

	TArray<uint8> Visited;
	Visited.SetNumZeroed(Mask.Num());
	const int32 MinBlob = FMath::Max(8, FMath::RoundToInt(Mask.Num() * 0.015f));
	int32 Blobs = 0;
	TArray<int32> Stack;
	Stack.Reserve(64);

	for (int32 i = 0; i < Mask.Num(); ++i)
	{
		if (!Mask[i] || Visited[i])
		{
			continue;
		}
		int32 Area = 0;
		Stack.Reset();
		Stack.Add(i);
		Visited[i] = 1;
		while (Stack.Num() > 0)
		{
			const int32 Idx = Stack.Pop(EAllowShrinking::No);
			++Area;
			const int32 X = Idx % ZoneW;
			const int32 Y = Idx / ZoneW;
			auto TryPush = [&](int32 NX, int32 NY)
			{
				if (NX < 0 || NY < 0 || NX >= ZoneW || NY >= ZoneH)
				{
					return;
				}
				const int32 NIdx = NY * ZoneW + NX;
				if (!Mask[NIdx] || Visited[NIdx])
				{
					return;
				}
				Visited[NIdx] = 1;
				Stack.Add(NIdx);
			};
			TryPush(X + 1, Y);
			TryPush(X - 1, Y);
			TryPush(X, Y + 1);
			TryPush(X, Y - 1);
		}
		if (Area >= MinBlob)
		{
			++Blobs;
		}
	}

	if (Blobs <= 0)
	{
		return OccupancyFraction >= OccupancyFractionThreshold ? 1 : 0;
	}
	return FMath::Min(Blobs, 3);
}

void UGodfreyVisitorPresenceComponent::UpdateSenseState(float DeltaTime)
{
	const bool bAppearanceOccupied = bForceOccupied || bRawOccupied;
	const bool bMotionNow = bForceOccupied || bRawMotion;

	if (!bAppearanceOccupied)
	{
		MotionConfirmAccumulatedSeconds = 0.f;
	}
	else if (bMotionNow)
	{
		MotionConfirmAccumulatedSeconds += DeltaTime;
	}

	switch (VisitorSenseState)
	{
	case EGodfreyVisitorSenseState::Empty:
		if (bAppearanceOccupied)
		{
			EnterDwellRemaining = EnterDwellSeconds;
			SetSenseState(EGodfreyVisitorSenseState::Approaching);
		}
		break;

	case EGodfreyVisitorSenseState::Approaching:
		if (!bAppearanceOccupied)
		{
			EnterDwellRemaining = -1.f;
			MotionConfirmAccumulatedSeconds = 0.f;
			SetSenseState(EGodfreyVisitorSenseState::Empty);
		}
		else
		{
			EnterDwellRemaining -= DeltaTime;
			if (EnterDwellRemaining <= 0.f)
			{
				if (bForceOccupied || MotionConfirmAccumulatedSeconds >= MotionConfirmSeconds)
				{
					SetSenseState(EGodfreyVisitorSenseState::Present);
					TryPresenceEngage();
				}
				else
				{
					// Static scene change (chair, lighting) — absorb as the new empty room.
					UE_LOG(LogGodfreyVision, Log,
						TEXT("VisitorPresence: Approaching timed out without motion confirm (motionAcc=%.2fs need=%.2fs occ=%.3f mot=%.3f) — recapturing empty."),
						MotionConfirmAccumulatedSeconds,
						MotionConfirmSeconds,
						OccupancyFraction,
						MotionFraction);
					SetSenseState(EGodfreyVisitorSenseState::Empty);
					BeginEmptyBackgroundCapture(TEXT("static-occupancy"), false);
				}
			}
		}
		break;

	case EGodfreyVisitorSenseState::Present:
		if (!bAppearanceOccupied)
		{
			if (LeaveDwellSeconds <= KINDA_SMALL_NUMBER)
			{
				// Immediate leave — no multi-second wait before goodbye (audience still in earshot).
				SetSenseState(EGodfreyVisitorSenseState::Leaving);
				SetSenseState(EGodfreyVisitorSenseState::Empty);
				TryPresenceFarewell();
			}
			else
			{
				LeaveDwellRemaining = LeaveDwellSeconds;
				SetSenseState(EGodfreyVisitorSenseState::Leaving);
			}
		}
		else
		{
			TryPresenceEngage();
		}
		break;

	case EGodfreyVisitorSenseState::Leaving:
		if (bAppearanceOccupied)
		{
			LeaveDwellRemaining = -1.f;
			SetSenseState(EGodfreyVisitorSenseState::Present);
			TryPresenceEngage();
		}
		else
		{
			LeaveDwellRemaining -= DeltaTime;
			if (LeaveDwellRemaining <= 0.f)
			{
				SetSenseState(EGodfreyVisitorSenseState::Empty);
				TryPresenceFarewell();
			}
		}
		break;
	}
}

void UGodfreyVisitorPresenceComponent::SetSenseState(EGodfreyVisitorSenseState NewState)
{
	if (VisitorSenseState == NewState)
	{
		return;
	}
	const EGodfreyVisitorSenseState Previous = VisitorSenseState;
	VisitorSenseState = NewState;
	UE_LOG(LogGodfreyVision, Log,
		TEXT("VisitorPresence: sense %s -> %s count=%d occ=%.3f mot=%.3f force=%d device='%s'"),
		GodfreyVisitorPresencePrivate::SenseStateName(Previous),
		GodfreyVisitorPresencePrivate::SenseStateName(NewState),
		EstimatedVisitorCount,
		OccupancyFraction,
		MotionFraction,
		bForceOccupied ? 1 : 0,
		*ActiveDeviceDisplayName);
	OnVisitorSenseChanged.Broadcast(NewState, Previous, EstimatedVisitorCount);

	if (NewState == EGodfreyVisitorSenseState::Empty
		&& (Previous == EGodfreyVisitorSenseState::Present
			|| Previous == EGodfreyVisitorSenseState::Leaving)
		&& !bCapturingEmptyBackground)
	{
		LeaveRecaptureCountdown = FMath::Max(0.f, LeaveRecaptureDelaySeconds);
	}

	// New encounter after farewell / SeaIdle — allow presence Welcome again.
	if (NewState == EGodfreyVisitorSenseState::Empty
		&& Previous != EGodfreyVisitorSenseState::Leaving)
	{
		bPresenceFarewellRequested = false;
		bPendingFarewellSpeak = false;
	}
}

void UGodfreyVisitorPresenceComponent::TryPresenceEngage()
{
	if (!bEngageOnPresence || !PerformerState)
	{
		return;
	}
	if (VisitorSenseState != EGodfreyVisitorSenseState::Present)
	{
		return;
	}
	if (PerformerState->GetExhibitionPresence() != EGodfreyExhibitionPresence::SeaIdle)
	{
		return;
	}
	if (PerformerState->HasEngagedVisitor() || PerformerState->IsInDialog())
	{
		return;
	}

	if (AnimationBridge)
	{
		AnimationBridge->ArmPresenceWelcomeEngage();
	}
	// New encounter — clear prior absence-farewell latch so a later leave can farewell again.
	bPresenceFarewellRequested = false;
	bPendingFarewellSpeak = false;
	UE_LOG(LogGodfreyVision, Log,
		TEXT("VisitorPresence: engaging from presence (Welcome armed) count=%d speak=%d"),
		EstimatedVisitorCount,
		bSpeakWelcomeOnPresence ? 1 : 0);
	PerformerState->NotifyVisitorEngaged();
	RequestWelcomeSpeak();
}

void UGodfreyVisitorPresenceComponent::RequestWelcomeSpeak()
{
	if (!bSpeakWelcomeOnPresence)
	{
		return;
	}
	if (WelcomeSpeakPrompt.TrimStartAndEnd().IsEmpty())
	{
		UE_LOG(LogGodfreyVision, Warning, TEXT("VisitorPresence: welcome speak skipped — empty WelcomeSpeakPrompt."));
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WelcomeSpeakTimerHandle);
		if (WelcomeSpeakDelaySeconds > KINDA_SMALL_NUMBER)
		{
			World->GetTimerManager().SetTimer(
				WelcomeSpeakTimerHandle,
				this,
				&UGodfreyVisitorPresenceComponent::HandleWelcomeSpeakTimer,
				WelcomeSpeakDelaySeconds,
				false);
			return;
		}
	}
	HandleWelcomeSpeakTimer();
}

void UGodfreyVisitorPresenceComponent::HandleWelcomeSpeakTimer()
{
	UGodfreyDirectSpeechComponent* Speech = ResolveDirectSpeech();
	if (!Speech)
	{
		UE_LOG(LogGodfreyVision, Error,
			TEXT("VisitorPresence: welcome speak failed — no GodfreyDirectSpeechComponent (is game mic enabled?)."));
		return;
	}

	const FString Prompt = WelcomeSpeakPrompt.TrimStartAndEnd();
	const bool bOk = Speech->AskGodfrey(Prompt);
	UE_LOG(LogGodfreyVision, Log,
		TEXT("VisitorPresence: welcome speak AskGodfrey ok=%d prompt_len=%d"),
		bOk ? 1 : 0,
		Prompt.Len());
}

void UGodfreyVisitorPresenceComponent::TryPresenceFarewell()
{
	if (!bFarewellOnAbsence || !PerformerState)
	{
		return;
	}
	if (bPresenceFarewellRequested)
	{
		return;
	}
	if (!PerformerState->IsInDialog())
	{
		UE_LOG(LogGodfreyVision, Log,
			TEXT("VisitorPresence: leave→Empty but not in dialog — no farewell speak."));
		return;
	}

	bPresenceFarewellRequested = true;
	// Latch R12 farewell so the wave runs after the goodbye line finishes (or fallback timer).
	PerformerState->RequestConversationEnd(TEXT("webcam visitor left"));
	UE_LOG(LogGodfreyVision, Log,
		TEXT("VisitorPresence: absence farewell latched (R12) — requesting goodbye speak."));
	RequestFarewellSpeak();
}

void UGodfreyVisitorPresenceComponent::RequestFarewellSpeak()
{
	if (FarewellSpeakPrompt.TrimStartAndEnd().IsEmpty())
	{
		UE_LOG(LogGodfreyVision, Warning,
			TEXT("VisitorPresence: farewell speak skipped — empty FarewellSpeakPrompt (gesture still latched)."));
		return;
	}

	UGodfreyDirectSpeechComponent* Speech = ResolveDirectSpeech();
	if (Speech && Speech->IsStreaming())
	{
		bPendingFarewellSpeak = true;
		PendingFarewellSpeakCountdown = 45.f;
		UE_LOG(LogGodfreyVision, Log,
			TEXT("VisitorPresence: farewell speak deferred — DirectSpeech busy."));
		return;
	}
	if (PerformerState)
	{
		const EGodfreyPerformanceState PerfState = PerformerState->GetPerformanceState();
		if (PerfState == EGodfreyPerformanceState::Speaking
			|| PerfState == EGodfreyPerformanceState::Thinking)
		{
			bPendingFarewellSpeak = true;
			PendingFarewellSpeakCountdown = 45.f;
			UE_LOG(LogGodfreyVision, Log,
				TEXT("VisitorPresence: farewell speak deferred — performer state=%d."),
				static_cast<int32>(PerfState));
			return;
		}
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FarewellSpeakTimerHandle);
		World->GetTimerManager().SetTimer(
			FarewellSpeakTimerHandle,
			this,
			&UGodfreyVisitorPresenceComponent::HandleFarewellSpeakTimer,
			0.2f,
			false);
		return;
	}
	HandleFarewellSpeakTimer();
}

void UGodfreyVisitorPresenceComponent::HandleFarewellSpeakTimer()
{
	bPendingFarewellSpeak = false;
	UGodfreyDirectSpeechComponent* Speech = ResolveDirectSpeech();
	if (!Speech)
	{
		UE_LOG(LogGodfreyVision, Error,
			TEXT("VisitorPresence: farewell speak failed — no GodfreyDirectSpeechComponent."));
		return;
	}

	const FString Prompt = FarewellSpeakPrompt.TrimStartAndEnd();
	const bool bOk = Speech->AskGodfrey(Prompt);
	UE_LOG(LogGodfreyVision, Log,
		TEXT("VisitorPresence: farewell speak AskGodfrey ok=%d prompt_len=%d"),
		bOk ? 1 : 0,
		Prompt.Len());
	if (!bOk)
	{
		// Keep trying briefly if Brain/stream was busy; R12 fallback still waves if speech never starts.
		bPendingFarewellSpeak = true;
		PendingFarewellSpeakCountdown = 10.f;
	}
}

void UGodfreyVisitorPresenceComponent::TickPendingFarewellSpeak(float DeltaTime)
{
	if (!bPendingFarewellSpeak)
	{
		return;
	}
	PendingFarewellSpeakCountdown -= DeltaTime;
	if (PendingFarewellSpeakCountdown <= 0.f)
	{
		bPendingFarewellSpeak = false;
		UE_LOG(LogGodfreyVision, Warning,
			TEXT("VisitorPresence: farewell speak pending timed out — R12 fallback will run farewell gesture."));
		return;
	}

	UGodfreyDirectSpeechComponent* Speech = ResolveDirectSpeech();
	if (Speech && Speech->IsStreaming())
	{
		return;
	}
	if (PerformerState)
	{
		const EGodfreyPerformanceState PerfState = PerformerState->GetPerformanceState();
		if (PerfState == EGodfreyPerformanceState::Speaking
			|| PerfState == EGodfreyPerformanceState::Thinking)
		{
			return;
		}
	}
	HandleFarewellSpeakTimer();
}

void UGodfreyVisitorPresenceComponent::TickEmptyBaselineMaintenance(float DeltaTime)
{
	if (bStartupIgnoreActive || bCapturingEmptyBackground || !bBackgroundReady)
	{
		return;
	}

	if (LeaveRecaptureCountdown >= 0.f)
	{
		LeaveRecaptureCountdown -= DeltaTime;
		if (LeaveRecaptureCountdown <= 0.f)
		{
			LeaveRecaptureCountdown = -1.f;
			if (VisitorSenseState == EGodfreyVisitorSenseState::Empty && !bForceOccupied)
			{
				if (OccupancyFraction >= OccupancyLeaveFractionThreshold)
				{
					// Still looks occupied — do not bake a standing visitor into the empty model.
					LeaveRecaptureCountdown = LeaveRecaptureDelaySeconds;
					UE_LOG(LogGodfreyVision, Log,
						TEXT("VisitorPresence: skip leave recapture (occ=%.3f still above leave %.3f)."),
						OccupancyFraction,
						OccupancyLeaveFractionThreshold);
					return;
				}
				BeginEmptyBackgroundCapture(TEXT("leave"), false);
				return;
			}
		}
	}

	const bool bAppearanceOccupied = !bForceOccupied && bRawOccupied;
	const bool bMotionNow = bRawMotion;
	const bool bIdleSense = VisitorSenseState == EGodfreyVisitorSenseState::Empty
		|| VisitorSenseState == EGodfreyVisitorSenseState::Approaching;

	if (bIdleSense && bAppearanceOccupied && !bMotionNow)
	{
		StillnessRebaseAccumulatedSeconds += DeltaTime;
		if (StillnessRebaseAccumulatedSeconds >= StillnessRebaseSeconds)
		{
			StillnessRebaseAccumulatedSeconds = 0.f;
			BeginEmptyBackgroundCapture(TEXT("stillness"), true);
			return;
		}
	}
	else
	{
		StillnessRebaseAccumulatedSeconds = 0.f;
	}

	if (PeriodicEmptyRefreshSeconds > KINDA_SMALL_NUMBER
		&& VisitorSenseState == EGodfreyVisitorSenseState::Empty
		&& !bAppearanceOccupied
		&& !bMotionNow
		&& !bForceOccupied)
	{
		if (PeriodicEmptyRefreshCountdown < 0.f)
		{
			PeriodicEmptyRefreshCountdown = PeriodicEmptyRefreshSeconds;
		}
		PeriodicEmptyRefreshCountdown -= DeltaTime;
		if (PeriodicEmptyRefreshCountdown <= 0.f)
		{
			PeriodicEmptyRefreshCountdown = PeriodicEmptyRefreshSeconds;
			BeginEmptyBackgroundCapture(TEXT("periodic"), false);
		}
	}
	else if (VisitorSenseState != EGodfreyVisitorSenseState::Empty)
	{
		PeriodicEmptyRefreshCountdown = PeriodicEmptyRefreshSeconds;
	}
}

void UGodfreyVisitorPresenceComponent::TickOccupancyDebugLog(float DeltaTime)
{
	if (VisitorSenseState != EGodfreyVisitorSenseState::Present
		&& VisitorSenseState != EGodfreyVisitorSenseState::Leaving
		&& VisitorSenseState != EGodfreyVisitorSenseState::Approaching
		&& !bRawOccupied
		&& !bCapturingEmptyBackground)
	{
		return;
	}
	OccupancyDebugLogCountdown -= DeltaTime;
	if (OccupancyDebugLogCountdown > 0.f)
	{
		return;
	}
	OccupancyDebugLogCountdown = 5.f;
	UE_LOG(LogGodfreyVision, Log,
		TEXT("VisitorPresence: heartbeat sense=%s occ=%.3f mot=%.3f count=%d rawOcc=%d rawMot=%d emptyAge=%.0fs leaveDwell=%.1f"),
		GodfreyVisitorPresencePrivate::SenseStateName(VisitorSenseState),
		OccupancyFraction,
		MotionFraction,
		EstimatedVisitorCount,
		bRawOccupied ? 1 : 0,
		bRawMotion ? 1 : 0,
		GetEmptyBackgroundAgeSeconds(),
		LeaveDwellRemaining);
}

UGodfreyDirectSpeechComponent* UGodfreyVisitorPresenceComponent::ResolveDirectSpeech() const
{
	if (AActor* Owner = GetOwner())
	{
		if (UGodfreyDirectSpeechComponent* OnOwner = Owner->FindComponentByClass<UGodfreyDirectSpeechComponent>())
		{
			return OnOwner;
		}
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor))
			{
				continue;
			}
			if (UGodfreyDirectSpeechComponent* Found = Actor->FindComponentByClass<UGodfreyDirectSpeechComponent>())
			{
				return Found;
			}
		}
	}
	return nullptr;
}

void UGodfreyVisitorPresenceComponent::TickActivityRefresh(float DeltaTime)
{
	if (!bRefreshActivityWhilePresent || !PerformerState)
	{
		return;
	}
	if (VisitorSenseState != EGodfreyVisitorSenseState::Present
		&& VisitorSenseState != EGodfreyVisitorSenseState::Leaving)
	{
		return;
	}
	if (!PerformerState->IsInDialog())
	{
		return;
	}

	ActivityRefreshCountdown -= DeltaTime;
	if (ActivityRefreshCountdown > 0.f)
	{
		return;
	}
	ActivityRefreshCountdown = 5.f;
	PerformerState->NotifyVisitorActivity();
}

void UGodfreyVisitorPresenceComponent::ResolvePerformerComponents()
{
	if (AActor* Godfrey = ResolveGodfreyActor())
	{
		PerformerState = Godfrey->FindComponentByClass<UGodfreyPerformanceStateComponent>();
		AnimationBridge = Godfrey->FindComponentByClass<UGodfreyPerformerAnimationBridgeComponent>();
	}
}

AActor* UGodfreyVisitorPresenceComponent::ResolveGodfreyActor() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (!CharacterActorTag.IsNone())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (IsValid(Actor) && Actor->ActorHasTag(CharacterActorTag))
			{
				return Actor;
			}
		}
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}
		if (Actor->GetActorNameOrLabel() == TEXT("BP_002_Godfrey_Performer")
			|| Actor->GetActorNameOrLabel() == TEXT("BP_Godfrey_Performer")
			|| Actor->GetName().Contains(TEXT("BP_Godfrey_Performer")))
		{
			return Actor;
		}
	}
	return nullptr;
}

void UGodfreyVisitorPresenceComponent::EnsureDebugPreview()
{
#if UE_BUILD_SHIPPING
	return;
#else
	if (!bDebugPreviewVisible || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	if (bDebugOverlayAdded)
	{
		return;
	}

	if (!DebugImageWidget.IsValid())
	{
		DebugMediaBrush.SetResourceObject(MediaTexture);
		DebugMediaBrush.ImageSize = FVector2D(DebugPreviewWidth, DebugPreviewHeight);

		DebugStatusText = SNew(STextBlock)
			.Text(FText::FromString(TEXT("Webcam")))
			.ColorAndOpacity(FSlateColor(FLinearColor::White))
			.ShadowOffset(FVector2D(1.f, 1.f))
			.AutoWrapText(true)
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9));

		DebugZoneBorder = SNew(SBorder)
			.BorderBackgroundColor(FLinearColor(0.1f, 0.9f, 0.3f, 0.35f))
			.Padding(0.f);

		DebugImageWidget = SNew(SImage).Image(&DebugMediaBrush);

		DebugOverlayWidget = SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(DebugPreviewMargin))
			[
				SNew(SBox)
				.WidthOverride(DebugPreviewWidth)
				.HeightOverride(DebugPreviewHeight + 40.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					.VAlign(VAlign_Top)
					[
						SNew(SBox)
						.WidthOverride(DebugPreviewWidth)
						.HeightOverride(DebugPreviewHeight)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()
							[
								DebugImageWidget.ToSharedRef()
							]
							+ SOverlay::Slot()
							.Padding(FMargin(
								DebugPreviewWidth * ZoneMinUV.X,
								DebugPreviewHeight * ZoneMinUV.Y,
								DebugPreviewWidth * (1.f - ZoneMaxUV.X),
								DebugPreviewHeight * (1.f - ZoneMaxUV.Y)))
							[
								DebugZoneBorder.ToSharedRef()
							]
						]
					]
					+ SOverlay::Slot()
					.VAlign(VAlign_Bottom)
					.Padding(FMargin(4.f, 0.f, 4.f, 2.f))
					[
						DebugStatusText.ToSharedRef()
					]
				]
			];
	}

	GEngine->GameViewport->AddViewportWidgetContent(DebugOverlayWidget.ToSharedRef(), 10000);
	bDebugOverlayAdded = true;
#endif
}

void UGodfreyVisitorPresenceComponent::TearDownDebugPreview()
{
	if (bDebugOverlayAdded && GEngine && GEngine->GameViewport && DebugOverlayWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(DebugOverlayWidget.ToSharedRef());
	}
	bDebugOverlayAdded = false;
}

void UGodfreyVisitorPresenceComponent::UpdateDebugPreviewLayout()
{
#if !UE_BUILD_SHIPPING
	if (!bDebugPreviewVisible)
	{
		return;
	}
	if (!bDebugOverlayAdded)
	{
		EnsureDebugPreview();
	}
	if (MediaTexture)
	{
		DebugMediaBrush.SetResourceObject(MediaTexture);
		DebugMediaBrush.ImageSize = FVector2D(DebugPreviewWidth, DebugPreviewHeight);
	}
	if (DebugStatusText.IsValid())
	{
		FString Phase = IsWebcamOpen() ? TEXT("live") : (bMediaOpenPending ? TEXT("opening") : TEXT("closed"));
		if (bStartupIgnoreActive)
		{
			Phase = FString::Printf(TEXT("IGNORE %.1fs — step out"), StartupIgnoreRemaining);
		}
		else if (bCapturingEmptyBackground)
		{
			Phase = FString::Printf(
				TEXT("EMPTY CAPTURE %d/%d"),
				EmptyBackgroundFramesCaptured,
				FMath::Max(1, EmptyBackgroundCaptureFrames));
		}
		const float EmptyAge = GetEmptyBackgroundAgeSeconds();
		FString EmptyAgeText = TEXT("empty --");
		if (bCapturingEmptyBackground)
		{
			EmptyAgeText = TEXT("empty recapturing");
		}
		else if (EmptyAge >= 0.f)
		{
			EmptyAgeText = EmptyAge >= 60.f
				? FString::Printf(TEXT("empty %.1fm"), EmptyAge / 60.f)
				: FString::Printf(TEXT("empty %.0fs"), EmptyAge);
		}
		const FString Line = FString::Printf(
			TEXT("%s | %s | n~%d | occ=%.0f%% mot=%.0f%% | %s%s\n%s"),
			ActiveDeviceDisplayName.IsEmpty() ? TEXT("no cam") : *ActiveDeviceDisplayName,
			GodfreyVisitorPresencePrivate::SenseStateName(VisitorSenseState),
			EstimatedVisitorCount,
			OccupancyFraction * 100.f,
			MotionFraction * 100.f,
			*Phase,
			bForceOccupied ? TEXT(" | FORCE") : TEXT(""),
			*EmptyAgeText);
		DebugStatusText->SetText(FText::FromString(Line));
	}
	if (DebugZoneBorder.IsValid())
	{
		FLinearColor ZoneColor = FLinearColor(0.1f, 0.9f, 0.3f, 0.3f);
		if (bStartupIgnoreActive || bCapturingEmptyBackground)
		{
			ZoneColor = FLinearColor(0.95f, 0.85f, 0.1f, 0.4f); // yellow while bootstrapping
		}
		else if (bRawOccupied || bForceOccupied)
		{
			ZoneColor = FLinearColor(0.95f, 0.2f, 0.15f, 0.4f);
		}
		DebugZoneBorder->SetBorderBackgroundColor(ZoneColor);
	}
#endif
}

void UGodfreyVisitorPresenceComponent::DrawDebugStatusText() const
{
#if !UE_BUILD_SHIPPING
	if (!bDebugPreviewVisible || !GEngine)
	{
		return;
	}
	// Keep a stable key so we don't spam the message stack; preview slate already shows detail.
	GEngine->AddOnScreenDebugMessage(
		0x57454341, // 'WECA'
		0.f,
		FColor::Cyan,
		FString::Printf(
			TEXT("Webcam presence [%s] n~%d occ=%.0f%% mot=%.0f%%  (F9 preview, F10 force, F11 recapture)"),
			GodfreyVisitorPresencePrivate::SenseStateName(VisitorSenseState),
			EstimatedVisitorCount,
			OccupancyFraction * 100.f,
			MotionFraction * 100.f));
#endif
}

float UGodfreyVisitorPresenceComponent::Luminance01(const FColor& C)
{
	return (0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B) / 255.f;
}
