#include "GodfreyRuntimePerfHudComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GodfreyDiagnostics.h"
#include "UnrealPerformerGodfreySettings.h"

UGodfreyRuntimePerfHudComponent::UGodfreyRuntimePerfHudComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bHudVisible = true;
}

void UGodfreyRuntimePerfHudComponent::BeginPlay()
{
	Super::BeginPlay();
#if UE_BUILD_SHIPPING
	bHudVisible = false;
	SetComponentTickEnabled(false);
#else
	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	bHudVisible = bStartVisible && Settings->bGodfreyShowRuntimePerfHud;
	SetComponentTickEnabled(bHudVisible || Settings->bGodfreyShowRuntimePerfHud);
#endif
}

void UGodfreyRuntimePerfHudComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
		const FKey ToggleKey = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyPerfHudToggleKey;
		if (PC->WasInputKeyJustPressed(ToggleKey))
		{
			bHudVisible = !bHudVisible;
		}
	}

	if (bHudVisible)
	{
		DrawHud();
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
