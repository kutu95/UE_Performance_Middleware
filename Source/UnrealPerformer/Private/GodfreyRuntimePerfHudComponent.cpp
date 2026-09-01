#include "GodfreyRuntimePerfHudComponent.h"

#include "Brushes/SlateColorBrush.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GodfreyDiagnostics.h"
#include "GodfreyPerformerAnimationBridgeComponent.h"
#include "Styling/CoreStyle.h"
#include "UnrealPerformerGodfreySettings.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

UGodfreyRuntimePerfHudComponent::UGodfreyRuntimePerfHudComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bHudVisible = true;
	bAnimOverlayVisible = true;
}

void UGodfreyRuntimePerfHudComponent::BeginPlay()
{
	Super::BeginPlay();
#if UE_BUILD_SHIPPING
	bHudVisible = false;
	bAnimOverlayVisible = false;
	SetComponentTickEnabled(false);
#else
	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	bHudVisible = bStartVisible && Settings->bGodfreyShowRuntimePerfHud;
	bAnimOverlayVisible = Settings->bGodfreyShowCurrentAnimHud;
	SetComponentTickEnabled(true);
	if (bAnimOverlayVisible)
	{
		EnsureAnimOverlay();
	}
#endif
}

void UGodfreyRuntimePerfHudComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TearDownAnimOverlay();
	Super::EndPlay(EndPlayReason);
}

void UGodfreyRuntimePerfHudComponent::SetHudVisible(bool bVisible)
{
	bHudVisible = bVisible;
}

void UGodfreyRuntimePerfHudComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if !UE_BUILD_SHIPPING
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
		if (PC->WasInputKeyJustPressed(Settings->GodfreyPerfHudToggleKey))
		{
			bHudVisible = !bHudVisible;
		}
		if (PC->WasInputKeyJustPressed(Settings->GodfreyCurrentAnimHudToggleKey))
		{
			bAnimOverlayVisible = !bAnimOverlayVisible;
			if (bAnimOverlayVisible)
			{
				EnsureAnimOverlay();
			}
			else if (AnimOverlayWidget.IsValid())
			{
				AnimOverlayWidget->SetVisibility(EVisibility::Collapsed);
			}
		}
	}

	if (bHudVisible)
	{
		DrawHud();
	}
	if (bAnimOverlayVisible)
	{
		EnsureAnimOverlay();
		UpdateAnimOverlay();
	}
#endif
}

void UGodfreyRuntimePerfHudComponent::DrawHud() const
{
#if !UE_BUILD_SHIPPING
	if (!GEngine)
	{
		return;
	}

	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	const float Fps = (Dt > KINDA_SMALL_NUMBER) ? (1.f / Dt) : 0.f;
	const float FrameMs = Dt * 1000.f;

	FString SpeechId = TEXT("(none)");
	FString Behaviour = TEXT("?");
	FString Anim = TEXT("?");
	FString Emotion = TEXT("(reserved)");
	int32 QueueLen = 0;
	float SpeechLatency = -1.f;
	int32 Ordinal = 0;

	if (const UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(this))
	{
		SpeechId = Diag->GetCurrentSpeechId().IsEmpty() ? TEXT("(none)") : Diag->GetCurrentSpeechId();
		Behaviour = Diag->GetBehaviourStateName();
		Anim = Diag->GetCurrentAnimationName();
		QueueLen = Diag->GetQueueLength();
		SpeechLatency = Diag->GetSpeechLatencyMs();
		Ordinal = Diag->GetCurrentUtteranceOrdinal();
	}

	const FString Line = FString::Printf(
		TEXT("Godfrey Perf | FPS=%.0f Frame=%.1fms | SpeechId=%s Ord=%d | Latency=%.0fms | Queue=%d | State=%s | Anim=%s | Emotion=%s | GPU/CPU=(stat unit)"),
		Fps,
		FrameMs,
		*SpeechId,
		Ordinal,
		SpeechLatency,
		QueueLen,
		*Behaviour,
		*Anim,
		*Emotion);

	GEngine->AddOnScreenDebugMessage(0x474F4446 /* 'GODF' */, 0.f, FColor::Green, Line);
#endif
}

void UGodfreyRuntimePerfHudComponent::EnsureAnimOverlay()
{
#if !UE_BUILD_SHIPPING
	if (bAnimOverlayAdded || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	if (!AnimNameText.IsValid())
	{
		if (!AnimBackingBrush.IsValid())
		{
			AnimBackingBrush = MakeShared<FSlateColorBrush>(FLinearColor::White);
		}

		AnimContextText = SNew(STextBlock)
			.Text(FText::FromString(TEXT("")))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.74f, 0.62f, 0.95f)))
			.ShadowOffset(FVector2D(1.f, 1.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.7f));

		AnimNameText = SNew(STextBlock)
			.Text(FText::FromString(TEXT("(none)")))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 22))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.98f, 0.96f, 0.88f, 1.f)))
			.ShadowOffset(FVector2D(1.f, 1.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));

		AnimOverlayWidget = SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(FMargin(18.f, 18.f))
			[
				SNew(SBorder)
				.BorderImage(AnimBackingBrush.Get())
				.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.04f, 0.78f))
				.Padding(FMargin(14.f, 10.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Playing")))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.6f, 0.52f, 0.9f)))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 2.f, 0.f, 0.f))
					[
						AnimNameText.ToSharedRef()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 2.f, 0.f, 0.f))
					[
						AnimContextText.ToSharedRef()
					]
				]
			];
	}

	GEngine->GameViewport->AddViewportWidgetContent(AnimOverlayWidget.ToSharedRef(), 10002);
	bAnimOverlayAdded = true;
	if (AnimOverlayWidget.IsValid())
	{
		AnimOverlayWidget->SetVisibility(bAnimOverlayVisible ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	}
#endif
}

void UGodfreyRuntimePerfHudComponent::TearDownAnimOverlay()
{
	if (bAnimOverlayAdded && GEngine && GEngine->GameViewport && AnimOverlayWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(AnimOverlayWidget.ToSharedRef());
	}
	bAnimOverlayAdded = false;
}

void UGodfreyRuntimePerfHudComponent::UpdateAnimOverlay()
{
#if !UE_BUILD_SHIPPING
	if (!AnimNameText.IsValid() || !AnimOverlayWidget.IsValid())
	{
		return;
	}

	AnimOverlayWidget->SetVisibility(EVisibility::HitTestInvisible);

	FString SequenceName = TEXT("(none)");
	FString ContextName;
	if (const UGodfreyPerformerAnimationBridgeComponent* Bridge = ResolveBridge())
	{
		SequenceName = Bridge->GetDebugPlayingSequenceName();
		ContextName = Bridge->GetDebugPlayingContextName();
	}
	else if (const UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(this))
	{
		SequenceName = Diag->GetCurrentAnimationName();
		ContextName = Diag->GetCurrentAnimationContext();
	}

	AnimNameText->SetText(FText::FromString(SequenceName));
	if (AnimContextText.IsValid())
	{
		AnimContextText->SetText(FText::FromString(ContextName));
		AnimContextText->SetVisibility(ContextName.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	}
#endif
}

UGodfreyPerformerAnimationBridgeComponent* UGodfreyRuntimePerfHudComponent::ResolveBridge() const
{
	if (UGodfreyPerformerAnimationBridgeComponent* Cached = CachedBridge.Get())
	{
		return Cached;
	}
	if (AActor* Owner = GetOwner())
	{
		if (UGodfreyPerformerAnimationBridgeComponent* Bridge =
			Owner->FindComponentByClass<UGodfreyPerformerAnimationBridgeComponent>())
		{
			CachedBridge = Bridge;
			return Bridge;
		}
	}
	return nullptr;
}
