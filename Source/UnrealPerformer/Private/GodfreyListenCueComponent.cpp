#include "GodfreyListenCueComponent.h"

#include "Brushes/SlateColorBrush.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontCache.h"
#include "GodfreyDiagnostics.h"
#include "GodfreyVoiceInputComponent.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

UGodfreyListenCueComponent::UGodfreyListenCueComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UGodfreyListenCueComponent::BeginPlay()
{
	Super::BeginPlay();

	const FString Dir = FPaths::ProjectContentDir() / TEXT("UI/GodfreyListenCue");
	SpeakLanternTexture = LoadPngTexture(Dir / TEXT("lantern_speak.png"), TEXT("GodfreyLanternSpeak"));
	WaitLanternTexture = LoadPngTexture(Dir / TEXT("lantern_wait.png"), TEXT("GodfreyLanternWait"));

	if (SpeakLanternTexture)
	{
		SpeakBrush = FSlateBrush();
		SpeakBrush.SetResourceObject(SpeakLanternTexture);
		SpeakBrush.ImageSize = FVector2D(LanternSize, LanternSize);
		SpeakBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	if (WaitLanternTexture)
	{
		WaitBrush = FSlateBrush();
		WaitBrush.SetResourceObject(WaitLanternTexture);
		WaitBrush.ImageSize = FVector2D(LanternSize, LanternSize);
		WaitBrush.DrawAs = ESlateBrushDrawType::Image;
	}

	if (bShowCue)
	{
		EnsureOverlay();
		UpdateCueVisual(ResolveCanVisitorSpeak());
	}

	UE_LOG(LogGodfreyVision, Log,
		TEXT("ListenCue: BeginPlay show=%d labels=%d speakTex=%d waitTex=%d"),
		bShowCue ? 1 : 0,
		bShowLabels ? 1 : 0,
		SpeakLanternTexture ? 1 : 0,
		WaitLanternTexture ? 1 : 0);
}

void UGodfreyListenCueComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TearDownOverlay();
	Super::EndPlay(EndPlayReason);
}

void UGodfreyListenCueComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bShowCue)
	{
		if (bOverlayAdded)
		{
			TearDownOverlay();
		}
		return;
	}

	EnsureOverlay();
	const bool bCanSpeak = ResolveCanVisitorSpeak();
	if (!bHasLastCanSpeak || bCanSpeak != bLastCanSpeak)
	{
		UpdateCueVisual(bCanSpeak);
		bLastCanSpeak = bCanSpeak;
		bHasLastCanSpeak = true;
	}
}

UTexture2D* UGodfreyListenCueComponent::LoadPngTexture(const FString& AbsolutePath, const TCHAR* ObjectName)
{
	if (!FPaths::FileExists(AbsolutePath))
	{
		UE_LOG(LogGodfreyVision, Error, TEXT("ListenCue: missing image '%s'"), *AbsolutePath);
		return nullptr;
	}
	UTexture2D* Tex = FImageUtils::ImportFileAsTexture2D(AbsolutePath);
	if (!Tex)
	{
		UE_LOG(LogGodfreyVision, Error, TEXT("ListenCue: failed to load '%s'"), *AbsolutePath);
		return nullptr;
	}
	Tex->Rename(ObjectName, this);
	Tex->Filter = TF_Bilinear;
	Tex->LODGroup = TEXTUREGROUP_UI;
	Tex->SRGB = true;
	Tex->UpdateResource();
	return Tex;
}

FSlateFontInfo UGodfreyListenCueComponent::MakePeriodLabelFont() const
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
		return FSlateFontInfo(Composite, LabelFontSize, TEXT("Regular"));
	}
	return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), static_cast<int32>(LabelFontSize));
}

UGodfreyVoiceInputComponent* UGodfreyListenCueComponent::ResolveVoiceInput() const
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

bool UGodfreyListenCueComponent::ResolveCanVisitorSpeak() const
{
	if (const UGodfreyVoiceInputComponent* Voice = ResolveVoiceInput())
	{
		return Voice->CanVisitorSpeak();
	}
	// No mic path — default to Wait so we never falsely invite speech.
	return false;
}

void UGodfreyListenCueComponent::EnsureOverlay()
{
	if (bOverlayAdded || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	if (!LanternImage.IsValid())
	{
		LanternImage = SNew(SImage).Image(&WaitBrush);
		LabelText = SNew(STextBlock)
			.Text(FText::FromString(TEXT("Wait")))
			.Font(MakePeriodLabelFont())
			.ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.86f, 0.72f, 0.95f)))
			.ShadowOffset(FVector2D(1.f, 1.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.65f))
			.Justification(ETextJustify::Center)
			.AutoWrapText(false);

		if (!BackingBrush.IsValid())
		{
			BackingBrush = MakeShared<FSlateColorBrush>(FLinearColor::White);
		}

		BackingBorder = SNew(SBorder)
			.BorderImage(BackingBrush.Get())
			.BorderBackgroundColor(FLinearColor(0.07f, 0.06f, 0.04f, 0.82f))
			.Padding(FMargin(BackingPadding, BackingPadding, BackingPadding, BackingPadding))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Top)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(LanternSize)
					.HeightOverride(LanternSize)
					[
						LanternImage.ToSharedRef()
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(FMargin(4.f, 8.f, 4.f, 2.f))
				[
					LabelText.ToSharedRef()
				]
			];

		OverlayWidget = SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			.Padding(FMargin(Margin))
			[
				BackingBorder.ToSharedRef()
			];
	}

	GEngine->GameViewport->AddViewportWidgetContent(OverlayWidget.ToSharedRef(), 10001);
	bOverlayAdded = true;

	if (LabelText.IsValid())
	{
		LabelText->SetVisibility(bShowLabels ? EVisibility::Visible : EVisibility::Collapsed);
	}
}

void UGodfreyListenCueComponent::TearDownOverlay()
{
	if (bOverlayAdded && GEngine && GEngine->GameViewport && OverlayWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(OverlayWidget.ToSharedRef());
	}
	bOverlayAdded = false;
}

void UGodfreyListenCueComponent::UpdateCueVisual(bool bCanSpeak)
{
	if (!LanternImage.IsValid())
	{
		return;
	}

	SpeakBrush.ImageSize = FVector2D(LanternSize, LanternSize);
	WaitBrush.ImageSize = FVector2D(LanternSize, LanternSize);
	LanternImage->SetImage(bCanSpeak ? &SpeakBrush : &WaitBrush);

	if (LabelText.IsValid() && bShowLabels)
	{
		LabelText->SetText(FText::FromString(bCanSpeak ? TEXT("Speak") : TEXT("Wait")));
		LabelText->SetColorAndOpacity(FSlateColor(
			bCanSpeak
				? FLinearColor(0.75f, 0.92f, 0.7f, 0.95f)
				: FLinearColor(0.92f, 0.72f, 0.62f, 0.95f)));
	}

	UE_LOG(LogGodfreyVision, Verbose, TEXT("ListenCue: %s"), bCanSpeak ? TEXT("Speak") : TEXT("Wait"));
}
