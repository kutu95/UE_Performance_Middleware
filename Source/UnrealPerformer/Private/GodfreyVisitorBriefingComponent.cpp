#include "GodfreyVisitorBriefingComponent.h"

#include "Brushes/SlateColorBrush.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fonts/CompositeFont.h"
#include "GameFramework/PlayerController.h"
#include "GodfreyDiagnostics.h"
#include "GodfreyVoiceInputComponent.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "UnrealPerformerGodfreySettings.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace GodfreyVisitorBriefingPrivate
{
static const TCHAR* DefaultBody =
	TEXT("This is a way into the past. It is far.\n")
	TEXT("\n")
	TEXT("When the lantern is GREEN, only one may speak.\n")
	TEXT("\n")
	TEXT("Speak clearly into the microphone.\n")
	TEXT("\n")
	TEXT("When it is RED, he cannot hear you.\n")
	TEXT("\n")
	TEXT("Give him a moment. The way is not swift.");
}

UGodfreyVisitorBriefingComponent::UGodfreyVisitorBriefingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	BodyText = GodfreyVisitorBriefingPrivate::DefaultBody;
}

void UGodfreyVisitorBriefingComponent::BeginPlay()
{
	Super::BeginPlay();
	ApplyProjectSettingsDefaults();
	UE_LOG(LogGodfreyVision, Log,
		TEXT("VisitorBriefing: BeginPlay enabled=%d hold=%.1fs"),
		bEnabled ? 1 : 0,
		HoldSeconds);
}

void UGodfreyVisitorBriefingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FinishPlaying(false);
	Super::EndPlay(EndPlayReason);
}

void UGodfreyVisitorBriefingComponent::ApplyProjectSettingsDefaults()
{
	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	if (!Settings)
	{
		return;
	}

	bEnabled = Settings->bGodfreyEnableVisitorBriefing;
	if (!Settings->GodfreyVisitorBriefingText.IsEmpty())
	{
		BodyText = Settings->GodfreyVisitorBriefingText;
	}
	FadeInSeconds = Settings->GodfreyVisitorBriefingFadeInSeconds;
	HoldSeconds = Settings->GodfreyVisitorBriefingHoldSeconds;
	FadeOutSeconds = Settings->GodfreyVisitorBriefingFadeOutSeconds;
}

void UGodfreyVisitorBriefingComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bPlaying)
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (SkipKey.IsValid() && PC->WasInputKeyJustPressed(SkipKey))
		{
			UE_LOG(LogGodfreyVision, Log, TEXT("VisitorBriefing: operator skip '%s'."), *SkipKey.ToString());
			SkipToFinish();
			return;
		}
	}
#endif

	ElapsedSeconds += DeltaTime;
	ApplyVisual(GetPhaseAlpha(), GetScrollPixels());

	const float Total = FadeInSeconds + HoldSeconds + FadeOutSeconds;
	if (ElapsedSeconds >= Total)
	{
		FinishPlaying(true);
	}
}

bool UGodfreyVisitorBriefingComponent::TryPlay()
{
	if (!bEnabled || bPlaying || bCompletedThisOccupancy)
	{
		return false;
	}

	ElapsedSeconds = 0.f;
	bPlaying = true;
	EnsureOverlay();
	ApplyVisual(0.f, 0.f);
	ApplyMicHold(true);
	UE_LOG(LogGodfreyVision, Log,
		TEXT("VisitorBriefing: playing (fadeIn=%.2f hold=%.2f fadeOut=%.2f)."),
		FadeInSeconds,
		HoldSeconds,
		FadeOutSeconds);
	return true;
}

void UGodfreyVisitorBriefingComponent::NotifyZoneEmpty()
{
	if (bPlaying)
	{
		UE_LOG(LogGodfreyVision, Log, TEXT("VisitorBriefing: cancelled (zone empty)."));
		FinishPlaying(false);
	}
	bCompletedThisOccupancy = false;
}

void UGodfreyVisitorBriefingComponent::NotifyNewEncounter()
{
	if (bPlaying)
	{
		UE_LOG(LogGodfreyVision, Log, TEXT("VisitorBriefing: restarting for new encounter."));
		FinishPlaying(false);
	}
	bCompletedThisOccupancy = false;
}

void UGodfreyVisitorBriefingComponent::SkipToFinish()
{
	if (!bPlaying)
	{
		return;
	}
	FinishPlaying(true);
}

void UGodfreyVisitorBriefingComponent::FinishPlaying(bool bBroadcastFinished)
{
	const bool bWasPlaying = bPlaying;
	TearDownOverlay();
	ApplyMicHold(false);
	bPlaying = false;
	ElapsedSeconds = 0.f;
	if (!bWasPlaying)
	{
		return;
	}
	if (bBroadcastFinished)
	{
		bCompletedThisOccupancy = true;
		OnBriefingFinished.Broadcast();
	}
}

void UGodfreyVisitorBriefingComponent::ApplyMicHold(bool bHold)
{
	if (UGodfreyVoiceInputComponent* Voice = ResolveVoiceInput())
	{
		Voice->SetBriefingHold(bHold);
	}
}

UGodfreyVoiceInputComponent* UGodfreyVisitorBriefingComponent::ResolveVoiceInput() const
{
	if (AActor* Owner = GetOwner())
	{
		if (UGodfreyVoiceInputComponent* OnOwner = Owner->FindComponentByClass<UGodfreyVoiceInputComponent>())
		{
			return OnOwner;
		}
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (UGodfreyVoiceInputComponent* Found = (*It)->FindComponentByClass<UGodfreyVoiceInputComponent>())
			{
				return Found;
			}
		}
	}
	return nullptr;
}

FSlateFontInfo UGodfreyVisitorBriefingComponent::MakePeriodBodyFont() const
{
	FString FontPath = FPaths::ProjectContentDir() / TEXT("UI/GodfreyListenCue/Constantia.ttf");
	if (!FPaths::FileExists(FontPath))
	{
		FontPath = TEXT("C:/Windows/Fonts/constan.ttf");
	}
	if (!FPaths::FileExists(FontPath))
	{
		FontPath = TEXT("C:/Windows/Fonts/georgia.ttf");
	}
	if (FPaths::FileExists(FontPath))
	{
		const TSharedRef<FCompositeFont> Composite = MakeShared<FCompositeFont>();
		Composite->DefaultTypeface.Fonts.Add(
			FTypefaceEntry(TEXT("Regular"), FontPath, EFontHinting::Default, EFontLoadingPolicy::LazyLoad));
		return FSlateFontInfo(Composite, BodyFontSize, TEXT("Regular"));
	}
	return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), static_cast<int32>(BodyFontSize));
}

float UGodfreyVisitorBriefingComponent::GetPhaseAlpha() const
{
	if (ElapsedSeconds <= FadeInSeconds)
	{
		return FMath::Clamp(ElapsedSeconds / FMath::Max(FadeInSeconds, KINDA_SMALL_NUMBER), 0.f, 1.f);
	}
	if (ElapsedSeconds <= FadeInSeconds + HoldSeconds)
	{
		return 1.f;
	}
	const float FadeT = (ElapsedSeconds - FadeInSeconds - HoldSeconds) / FMath::Max(FadeOutSeconds, KINDA_SMALL_NUMBER);
	return 1.f - FMath::Clamp(FadeT, 0.f, 1.f);
}

float UGodfreyVisitorBriefingComponent::GetScrollPixels() const
{
	if (HoldScrollPixels <= KINDA_SMALL_NUMBER || HoldSeconds <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	const float IntoHold = ElapsedSeconds - FadeInSeconds;
	const float HoldT = FMath::Clamp(IntoHold / HoldSeconds, 0.f, 1.f);
	return HoldT * HoldScrollPixels;
}

void UGodfreyVisitorBriefingComponent::ApplyVisual(float Alpha, float ScrollPixels)
{
	if (DimBorder.IsValid())
	{
		DimBorder->SetBorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, DimOpacity * Alpha));
	}
	if (BodyBlock.IsValid())
	{
		const FLinearColor Ivory(0.92f, 0.86f, 0.72f, Alpha);
		BodyBlock->SetColorAndOpacity(FSlateColor(Ivory));
		BodyBlock->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.7f * Alpha));
		BodyBlock->SetRenderTransform(FSlateRenderTransform(FVector2D(0.f, -ScrollPixels)));
		BodyBlock->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	}
}

void UGodfreyVisitorBriefingComponent::EnsureOverlay()
{
	if (bOverlayAdded || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	if (!OverlayWidget.IsValid())
	{
		const FString Copy = BodyText.TrimStartAndEnd().IsEmpty()
			? FString(GodfreyVisitorBriefingPrivate::DefaultBody)
			: BodyText;

		if (!DimBrush.IsValid())
		{
			DimBrush = MakeShared<FSlateColorBrush>(FLinearColor::White);
		}

		DimBorder = SNew(SBorder)
			.BorderImage(DimBrush.Get())
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.f))
			.Padding(0.f);

		BodyBlock = SNew(STextBlock)
			.Text(FText::FromString(Copy))
			.Font(MakePeriodBodyFont())
			.ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.86f, 0.72f, 0.f)))
			.ShadowOffset(FVector2D(1.5f, 1.5f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.f))
			.Justification(ETextJustify::Center)
			.AutoWrapText(true);

		OverlayWidget = SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				DimBorder.ToSharedRef()
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(80.f, 72.f, 80.f, 72.f))
			[
				SNew(SBox)
				.MaxDesiredWidth(980.f)
				[
					BodyBlock.ToSharedRef()
				]
			];
	}
	else if (BodyBlock.IsValid())
	{
		const FString Copy = BodyText.TrimStartAndEnd().IsEmpty()
			? FString(GodfreyVisitorBriefingPrivate::DefaultBody)
			: BodyText;
		BodyBlock->SetText(FText::FromString(Copy));
	}

	// Below the signal lantern (10001) so Speak/Wait stays visible during the card.
	GEngine->GameViewport->AddViewportWidgetContent(OverlayWidget.ToSharedRef(), 9999);
	bOverlayAdded = true;
}

void UGodfreyVisitorBriefingComponent::TearDownOverlay()
{
	if (bOverlayAdded && GEngine && GEngine->GameViewport && OverlayWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(OverlayWidget.ToSharedRef());
	}
	bOverlayAdded = false;
}
