#include "GodfreyVoiceInputComponent.h"

#include "AudioCaptureCore.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GodfreyDirectSpeechComponent.h"
#include "GodfreyExhibitionQueuePollComponent.h"
#include "GodfreyPcmStreamSession.h"
#include "GodfreyPerformanceStateComponent.h"
#include "GodfreyPerformanceTypes.h"
#include "GodfreyPerformerAnimationBridgeComponent.h"
#include "IWebSocket.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "WebSocketsModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogGodfreyVoiceInput, Log, All);

struct FGodfreyAudioCaptureState
{
	Audio::FAudioCapture Capture;
	bool bOpen = false;
};

namespace GodfreyVoiceInputPrivate
{
	static const TCHAR* SttPromptEchoNeedle = TEXT("exhibition visitor questions about the ss georgette");

	static FString NormalizeSttTranscript(const FString& Transcript)
	{
		FString T = Transcript.TrimStartAndEnd().ToLower();
		for (TCHAR& C : T)
		{
			if (!FChar::IsAlnum(C) && C != TEXT('\'') && C != TEXT('-'))
			{
				C = TEXT(' ');
			}
		}
		while (T.Contains(TEXT("  ")))
		{
			T.ReplaceInline(TEXT("  "), TEXT(" "));
		}
		return T.TrimStartAndEnd();
	}

	static bool LooksLikeSttPromptEcho(const FString& Transcript)
	{
		const FString T = Transcript.TrimStartAndEnd().ToLower();
		if (T.IsEmpty())
		{
			return true;
		}
		if (T.Contains(SttPromptEchoNeedle))
		{
			return true;
		}
		if (T.Contains(TEXT("captain john godfrey exhibition")) && T.Contains(TEXT("australian english")))
		{
			return true;
		}
		return false;
	}

	/** OpenAI transcribe often emits these on room tone / speaker tail. Never a visitor name. */
	static bool LooksLikeSttNoiseHallucination(const FString& Transcript)
	{
		const FString T = NormalizeSttTranscript(Transcript);
		if (T.IsEmpty())
		{
			return true;
		}
		static const TCHAR* Phrases[] = {
			TEXT("cool"),
			TEXT("hello"),
			TEXT("hi"),
			TEXT("hey"),
			TEXT("okay"),
			TEXT("ok"),
			TEXT("thanks"),
			TEXT("thank you"),
			TEXT("you"),
			TEXT("the"),
			TEXT("a"),
			TEXT("and"),
			TEXT("um"),
			TEXT("uh"),
			TEXT("hmm"),
			TEXT("mm"),
			TEXT("mhm"),
			TEXT("music"),
			TEXT("subtitle"),
			TEXT("subtitles"),
			TEXT("applause"),
			TEXT("thanks for watching"),
			TEXT("thank you for watching"),
		};
		for (const TCHAR* Phrase : Phrases)
		{
			if (T.Equals(Phrase))
			{
				return true;
			}
		}
		return false;
	}

	static bool LooksLikeVisitorFarewell(const FString& Transcript)
	{
		FString N = Transcript.TrimStartAndEnd().ToLower();
		if (N.IsEmpty() || N.Contains(TEXT("?")))
		{
			return false;
		}
		N.ReplaceInline(TEXT(","), TEXT(" "));
		N.ReplaceInline(TEXT("."), TEXT(" "));
		N.ReplaceInline(TEXT("!"), TEXT(" "));
		N.ReplaceInline(TEXT(";"), TEXT(" "));
		while (N.Contains(TEXT("  ")))
		{
			N.ReplaceInline(TEXT("  "), TEXT(" "));
		}
		N.TrimStartAndEndInline();

		const TCHAR* Phrases[] = {
			TEXT("goodbye"),
			TEXT("good bye"),
			TEXT("farewell"),
			TEXT("got to go"),
			TEXT("gotta go"),
			TEXT("have to go"),
			TEXT("must be off"),
			TEXT("that's all"),
			TEXT("thats all"),
			TEXT("see you"),
			TEXT("see ya"),
			TEXT("i'm off"),
			TEXT("im off"),
			TEXT("thanks for your time"),
			TEXT("thank you for your time"),
		};
		for (const TCHAR* Phrase : Phrases)
		{
			if (N.Contains(Phrase))
			{
				return true;
			}
		}
		return N.Equals(TEXT("bye")) || N.StartsWith(TEXT("bye ")) || N.EndsWith(TEXT(" bye"));
	}

	static AActor* ResolveGodfreyCharacter(const UGodfreyVoiceInputComponent* Voice)
	{
		if (!Voice)
		{
			return nullptr;
		}
		if (IsValid(Voice->DirectSpeech) && IsValid(Voice->DirectSpeech->CharacterForAce))
		{
			return Voice->DirectSpeech->CharacterForAce.Get();
		}
		if (const AActor* Owner = Voice->GetOwner())
		{
			if (const UGodfreyExhibitionQueuePollComponent* Poll =
				Owner->FindComponentByClass<UGodfreyExhibitionQueuePollComponent>())
			{
				if (IsValid(Poll->CharacterForAce))
				{
					return Poll->CharacterForAce.Get();
				}
			}
			if (UWorld* World = Voice->GetWorld())
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					AActor* Actor = *It;
					if (!IsValid(Actor))
					{
						continue;
					}
					if (Actor->GetActorNameOrLabel() == TEXT("BP_Godfrey_Performer")
						|| Actor->GetName().Contains(TEXT("BP_Godfrey_Performer")))
					{
						return Actor;
					}
				}
			}
		}
		return nullptr;
	}

	static FString HttpToWsBase(const FString& HttpBase)
	{
		FString Base = HttpBase.TrimStartAndEnd();
		while (Base.EndsWith(TEXT("/")))
		{
			Base.LeftChopInline(1);
		}
		if (Base.StartsWith(TEXT("https://")))
		{
			Base = TEXT("wss://") + Base.RightChop(8);
		}
		else if (Base.StartsWith(TEXT("http://")))
		{
			Base = TEXT("ws://") + Base.RightChop(7);
		}
		else if (!Base.StartsWith(TEXT("ws://")) && !Base.StartsWith(TEXT("wss://")))
		{
			Base = TEXT("ws://") + Base;
		}
		return Base;
	}

	static float ReadMonoSample(const float* InAudio, int32 FrameIndex, int32 NumChannels)
	{
		if (NumChannels <= 1)
		{
			return InAudio[FrameIndex];
		}
		float Sum = 0.f;
		for (int32 Ch = 0; Ch < NumChannels; ++Ch)
		{
			Sum += InAudio[FrameIndex * NumChannels + Ch];
		}
		return Sum / static_cast<float>(NumChannels);
	}
}

UGodfreyVoiceInputComponent::UGodfreyVoiceInputComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	CaptureState = new FGodfreyAudioCaptureState();
}

UGodfreyVoiceInputComponent::~UGodfreyVoiceInputComponent()
{
	// CaptureState type is incomplete in the header — destroy only here.
	CloseCaptureStream();
	delete CaptureState;
	CaptureState = nullptr;
}

void UGodfreyVoiceInputComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureDirectSpeech();

	if (bListenOnBeginPlay)
	{
		StartListening();
	}
}

void UGodfreyVoiceInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopListening();
	Super::EndPlay(EndPlayReason);
}

void UGodfreyVoiceInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bListening)
	{
		return;
	}

	const bool bSpeakingNow = IsGodfreySpeaking();
	if (bWasGodfreySpeaking && !bSpeakingNow)
	{
		if (PostSpeechIgnoreSeconds > KINDA_SMALL_NUMBER)
		{
			if (const UWorld* World = GetWorld())
			{
				PostSpeechIgnoreUntilWorldTime = World->GetTimeSeconds() + static_cast<double>(PostSpeechIgnoreSeconds);
				UE_LOG(LogGodfreyVoiceInput, Log,
					TEXT("GodfreyVoiceInput: post-speech STT ignore for %.2fs (echo guard)."),
					PostSpeechIgnoreSeconds);
			}
		}
		TrySendPendingFarewellTranscript();
	}
	bWasGodfreySpeaking = bSpeakingNow;

	if (bPauseWhileGodfreySpeaking)
	{
		const bool bShouldPause = IsGodfreySpeaking() || IsInPostSpeechIgnore();
		if (bShouldPause != bMicPaused)
		{
			SetMicPaused(bShouldPause);
		}
	}

	// R10: start conversational silence only when Speak is green (not during post-speech Wait).
	const bool bCanSpeakNow = CanVisitorSpeak();
	if (bCanSpeakNow && !bWasCanVisitorSpeak)
	{
		if (AActor* const Character = GodfreyVoiceInputPrivate::ResolveGodfreyCharacter(this))
		{
			if (UGodfreyPerformanceStateComponent* Perf =
				Character->FindComponentByClass<UGodfreyPerformanceStateComponent>())
			{
				Perf->NotifyListeningWindowOpened();
				UE_LOG(LogGodfreyVoiceInput, Log, TEXT("GodfreyVoiceInput: listening window open (Speak)."));
			}
		}
	}
	bWasCanVisitorSpeak = bCanSpeakNow;

	FlushCaptureToSocket();
}

void UGodfreyVoiceInputComponent::EnsureDirectSpeech()
{
	if (IsValid(DirectSpeech))
	{
		return;
	}

	if (AActor* Owner = GetOwner())
	{
		DirectSpeech = Owner->FindComponentByClass<UGodfreyDirectSpeechComponent>();
	}
}

bool UGodfreyVoiceInputComponent::IsInPostSpeechIgnore() const
{
	if (PostSpeechIgnoreUntilWorldTime < 0.0)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() < PostSpeechIgnoreUntilWorldTime;
}

bool UGodfreyVoiceInputComponent::CanVisitorSpeak() const
{
	if (!bListening || !bWantListening)
	{
		return false;
	}
	if (IsGodfreySpeaking() || IsInPostSpeechIgnore())
	{
		return false;
	}
	return true;
}

bool UGodfreyVoiceInputComponent::IsGodfreySpeaking() const
{
	if (IsValid(DirectSpeech) && DirectSpeech->IsStreaming())
	{
		return true;
	}

	if (const AActor* Owner = GetOwner())
	{
		if (const UGodfreyExhibitionQueuePollComponent* Poll = Owner->FindComponentByClass<UGodfreyExhibitionQueuePollComponent>())
		{
			if (Poll->IsStreamActive())
			{
				return true;
			}
		}
		if (const UGodfreyDirectSpeechComponent* Speech = Owner->FindComponentByClass<UGodfreyDirectSpeechComponent>())
		{
			if (Speech->IsStreaming())
			{
				return true;
			}
		}
	}

	// Hold-play: HTTP stream ends before audible starts — keep mic paused through Thinking/Speaking.
	// Also: HTTP FinishStream clears IsStreaming while ACE may still play for many seconds (speaker→mic feedback).
	if (AActor* const Character = GodfreyVoiceInputPrivate::ResolveGodfreyCharacter(this))
	{
		if (UGodfreyPcmStreamSession::IsCharacterAudiblePlaybackActive(Character))
		{
			return true;
		}
		if (const UGodfreyPerformanceStateComponent* Perf =
			Character->FindComponentByClass<UGodfreyPerformanceStateComponent>())
		{
			const EGodfreyPerformanceState State = Perf->GetPerformanceState();
			if (State == EGodfreyPerformanceState::Speaking
				|| State == EGodfreyPerformanceState::Thinking)
			{
				return true;
			}
		}
		if (const UGodfreyPerformerAnimationBridgeComponent* Bridge =
			Character->FindComponentByClass<UGodfreyPerformerAnimationBridgeComponent>())
		{
			if (Bridge->bIsSpeaking || Bridge->bIsThinking)
			{
				return true;
			}
		}
	}

	return false;
}

void UGodfreyVoiceInputComponent::StartListening()
{
	bWantListening = true;

	if (bListening)
	{
		return;
	}

	EnsureDirectSpeech();
	if (!IsValid(DirectSpeech))
	{
		const FString Err = TEXT("GodfreyVoiceInput: no GodfreyDirectSpeechComponent found (add it on the same actor).");
		UE_LOG(LogGodfreyVoiceInput, Error, TEXT("%s"), *Err);
		OnVoiceInputError.Broadcast(Err);
		return;
	}

	if (!OpenCaptureStream())
	{
		const FString Err = TEXT("GodfreyVoiceInput: failed to open microphone capture stream.");
		UE_LOG(LogGodfreyVoiceInput, Error, TEXT("%s"), *Err);
		OnVoiceInputError.Broadcast(Err);
		ScheduleReconnect();
		return;
	}

	ConnectSttWebSocket();
	bListening = true;
	bMicPaused = false;
	SetComponentTickEnabled(true);

	UE_LOG(LogGodfreyVoiceInput, Log,
		TEXT("GodfreyVoiceInput: listening started (capture=%d Hz → stream=%d Hz)."),
		CaptureSampleRate,
		StreamSampleRate);
}

void UGodfreyVoiceInputComponent::StopListening()
{
	bWantListening = false;
	ClearAwaitingFinalTranscript();
	bReconnectScheduled = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReconnectTimerHandle);
	}

	DisconnectSttWebSocket();
	CloseCaptureStream();

	{
		FScopeLock Lock(&CaptureMutex);
		PendingPcm16.Reset();
	}

	bListening = false;
	bMicPaused = false;
	bServerReady = false;
	bSpeechActive = false;
	SetComponentTickEnabled(false);

	UE_LOG(LogGodfreyVoiceInput, Log, TEXT("GodfreyVoiceInput: listening stopped."));
}

void UGodfreyVoiceInputComponent::SetMicPaused(bool bPaused)
{
	if (bMicPaused == bPaused)
	{
		return;
	}

	bMicPaused = bPaused;
	if (bPaused)
	{
		SendControl(TEXT("pause"));
	}
	else
	{
		SendControl(TEXT("clear"));
		SendControl(TEXT("resume"));
	}

	if (bPaused)
	{
		FScopeLock Lock(&CaptureMutex);
		PendingPcm16.Reset();
		bSpeechActive = false;
	}

	UE_LOG(LogGodfreyVoiceInput, Log, TEXT("GodfreyVoiceInput: mic %s"), bPaused ? TEXT("paused") : TEXT("resumed"));
}

bool UGodfreyVoiceInputComponent::OpenCaptureStream()
{
	CloseCaptureStream();
	if (!CaptureState)
	{
		CaptureState = new FGodfreyAudioCaptureState();
	}

	const int32 DeviceIndex = ResolveCaptureDeviceIndex();

	Audio::FAudioCaptureDeviceParams Params;
	Params.DeviceIndex = DeviceIndex;
	Params.NumInputChannels = FMath::Clamp(CaptureNumChannels, 1, 8);
	Params.SampleRate = FMath::Clamp(CaptureSampleRate, 8000, 48000);
	Params.bUseHardwareAEC = true;

	TWeakObjectPtr<UGodfreyVoiceInputComponent> WeakThis(this);
	const bool bOpened = CaptureState->Capture.OpenAudioCaptureStream(
		Params,
		[WeakThis](const void* InAudio, int32 NumFrames, int32 NumChannels, int32 SampleRate, double /*StreamTime*/, bool /*bOverflow*/)
		{
			UGodfreyVoiceInputComponent* Self = WeakThis.Get();
			if (!Self || !InAudio || NumFrames <= 0)
			{
				return;
			}
			Self->OnCaptureAudio(static_cast<const float*>(InAudio), NumFrames, NumChannels, SampleRate);
		},
		1024);

	if (!bOpened)
	{
		CaptureState->bOpen = false;
		UE_LOG(LogGodfreyVoiceInput, Error,
			TEXT("GodfreyVoiceInput: OpenAudioCaptureStream failed (deviceIndex=%d filter='%s')."),
			DeviceIndex,
			*PreferredCaptureDeviceNameFilter);
		return false;
	}

	if (!CaptureState->Capture.StartStream())
	{
		CaptureState->Capture.CloseStream();
		CaptureState->bOpen = false;
		UE_LOG(LogGodfreyVoiceInput, Error, TEXT("GodfreyVoiceInput: StartStream failed."));
		return false;
	}

	CaptureState->bOpen = true;
	return true;
}

int32 UGodfreyVoiceInputComponent::ResolveCaptureDeviceIndex() const
{
	if (!CaptureState)
	{
		UE_LOG(LogGodfreyVoiceInput, Warning,
			TEXT("GodfreyVoiceInput: ResolveCaptureDeviceIndex called with no CaptureState — using default."));
		return Audio::DefaultDeviceIndex;
	}

	TArray<Audio::FCaptureDeviceInfo> Devices;
	const int32 NumDevices = CaptureState->Capture.GetCaptureDevicesAvailable(Devices);
	UE_LOG(LogGodfreyVoiceInput, Log, TEXT("GodfreyVoiceInput: %d capture device(s) available:"), NumDevices);
	for (int32 i = 0; i < Devices.Num(); ++i)
	{
		UE_LOG(LogGodfreyVoiceInput, Log,
			TEXT("GodfreyVoiceInput:   [%d] '%s' (ch=%d rate=%d)"),
			i,
			*Devices[i].DeviceName,
			Devices[i].InputChannels,
			Devices[i].PreferredSampleRate);
	}

	if (PreferredCaptureDeviceNameFilter.IsEmpty())
	{
		UE_LOG(LogGodfreyVoiceInput, Log,
			TEXT("GodfreyVoiceInput: using system default capture device (no PreferredCaptureDeviceNameFilter)."));
		return Audio::DefaultDeviceIndex;
	}

	for (int32 i = 0; i < Devices.Num(); ++i)
	{
		if (Devices[i].DeviceName.Contains(PreferredCaptureDeviceNameFilter, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogGodfreyVoiceInput, Log,
				TEXT("GodfreyVoiceInput: selected capture device [%d] '%s' (filter='%s')."),
				i,
				*Devices[i].DeviceName,
				*PreferredCaptureDeviceNameFilter);
			return i;
		}
	}

	UE_LOG(LogGodfreyVoiceInput, Warning,
		TEXT("GodfreyVoiceInput: no capture device matched filter '%s' — falling back to system default (often the webcam mic)."),
		*PreferredCaptureDeviceNameFilter);
	return Audio::DefaultDeviceIndex;
}

void UGodfreyVoiceInputComponent::CloseCaptureStream()
{
	if (!CaptureState || !CaptureState->bOpen)
	{
		return;
	}

	CaptureState->Capture.StopStream();
	CaptureState->Capture.CloseStream();
	CaptureState->bOpen = false;
}

void UGodfreyVoiceInputComponent::OnCaptureAudio(const float* InAudio, int32 NumFrames, int32 NumChannels, int32 SampleRate)
{
	if (!bListening || bMicPaused || !InAudio || NumFrames <= 0)
	{
		return;
	}

	AppendResampledPcm16(InAudio, NumFrames, NumChannels, SampleRate);
}

void UGodfreyVoiceInputComponent::AppendResampledPcm16(const float* InAudio, int32 NumFrames, int32 NumChannels, int32 InSampleRate)
{
	const int32 SafeChannels = FMath::Max(1, NumChannels);
	const int32 SafeInRate = FMath::Max(8000, InSampleRate);
	const int32 OutRate = StreamSampleRate;

	TArray<uint8> LocalPcm;
	LocalPcm.Reserve((NumFrames * OutRate / SafeInRate + 8) * sizeof(int16));

	if (SafeInRate == OutRate)
	{
		for (int32 Frame = 0; Frame < NumFrames; ++Frame)
		{
			const float Sample = FMath::Clamp(GodfreyVoiceInputPrivate::ReadMonoSample(InAudio, Frame, SafeChannels), -1.f, 1.f);
			const int16 Pcm = static_cast<int16>(Sample * 32767.f);
			LocalPcm.Append(reinterpret_cast<const uint8*>(&Pcm), sizeof(int16));
		}
	}
	else
	{
		// Linear resample to 24 kHz mono.
		const double Step = static_cast<double>(SafeInRate) / static_cast<double>(OutRate);
		const int32 OutFrames = FMath::Max(1, FMath::FloorToInt(static_cast<float>(NumFrames) * static_cast<float>(OutRate) / static_cast<float>(SafeInRate)));
		for (int32 OutFrame = 0; OutFrame < OutFrames; ++OutFrame)
		{
			const double SrcPos = OutFrame * Step;
			const int32 Idx = FMath::Clamp(static_cast<int32>(SrcPos), 0, NumFrames - 1);
			const int32 IdxNext = FMath::Min(Idx + 1, NumFrames - 1);
			const float Frac = static_cast<float>(SrcPos - Idx);
			const float A = GodfreyVoiceInputPrivate::ReadMonoSample(InAudio, Idx, SafeChannels);
			const float B = GodfreyVoiceInputPrivate::ReadMonoSample(InAudio, IdxNext, SafeChannels);
			const float Sample = FMath::Clamp(FMath::Lerp(A, B, Frac), -1.f, 1.f);
			const int16 Pcm = static_cast<int16>(Sample * 32767.f);
			LocalPcm.Append(reinterpret_cast<const uint8*>(&Pcm), sizeof(int16));
		}
	}

	if (LocalPcm.Num() == 0)
	{
		return;
	}

	FScopeLock Lock(&CaptureMutex);
	PendingPcm16.Append(LocalPcm);
	// Bound memory if the socket stalls (~2s @ 24 kHz mono).
	const int32 MaxBytes = StreamSampleRate * sizeof(int16) * 2;
	if (PendingPcm16.Num() > MaxBytes)
	{
		const int32 Overflow = PendingPcm16.Num() - MaxBytes;
		PendingPcm16.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
}

void UGodfreyVoiceInputComponent::FlushCaptureToSocket()
{
	if (!SttSocket.IsValid() || !SttSocket->IsConnected() || !bServerReady || bMicPaused)
	{
		return;
	}

	TArray<uint8> ToSend;
	{
		FScopeLock Lock(&CaptureMutex);
		if (PendingPcm16.Num() < 2)
		{
			return;
		}
		ToSend = MoveTemp(PendingPcm16);
		PendingPcm16.Reset();
	}

	// Keep even byte count for PCM16.
	if (ToSend.Num() % 2 != 0)
	{
		ToSend.Pop(EAllowShrinking::No);
	}
	if (ToSend.Num() == 0)
	{
		return;
	}

	SttSocket->Send(ToSend.GetData(), ToSend.Num(), /*bIsBinary*/ true);
}

void UGodfreyVoiceInputComponent::ConnectSttWebSocket()
{
	DisconnectSttWebSocket();
	bServerReady = false;

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("WebSockets")))
	{
		FModuleManager::Get().LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	}

	const FString Url = GodfreyVoiceInputPrivate::HttpToWsBase(GodfreyBrainBaseUrl) + TEXT("/api/unreal/stt");
	SttSocket = FWebSocketsModule::Get().CreateWebSocket(Url, TEXT(""));

	if (!SttSocket.IsValid())
	{
		const FString Err = FString::Printf(TEXT("GodfreyVoiceInput: failed to create websocket for %s"), *Url);
		UE_LOG(LogGodfreyVoiceInput, Error, TEXT("%s"), *Err);
		OnVoiceInputError.Broadcast(Err);
		ScheduleReconnect();
		return;
	}

	TWeakObjectPtr<UGodfreyVoiceInputComponent> WeakThis(this);

	SttSocket->OnConnected().AddLambda([WeakThis]()
	{
		if (UGodfreyVoiceInputComponent* Self = WeakThis.Get())
		{
			UE_LOG(LogGodfreyVoiceInput, Log, TEXT("GodfreyVoiceInput: STT websocket connected."));
			Self->bReconnectScheduled = false;
		}
	});

	SttSocket->OnConnectionError().AddLambda([WeakThis](const FString& Error)
	{
		if (UGodfreyVoiceInputComponent* Self = WeakThis.Get())
		{
			const FString Err = FString::Printf(TEXT("STT websocket connection error: %s"), *Error);
			UE_LOG(LogGodfreyVoiceInput, Error, TEXT("%s"), *Err);
			Self->bServerReady = false;
			Self->OnVoiceInputError.Broadcast(Err);
			Self->ScheduleReconnect();
		}
	});

	SttSocket->OnClosed().AddLambda([WeakThis](int32 StatusCode, const FString& Reason, bool /*bWasClean*/)
	{
		if (UGodfreyVoiceInputComponent* Self = WeakThis.Get())
		{
			UE_LOG(LogGodfreyVoiceInput, Warning,
				TEXT("GodfreyVoiceInput: STT websocket closed code=%d reason=%s"),
				StatusCode,
				*Reason);
			Self->bServerReady = false;
			if (Self->bWantListening)
			{
				Self->ScheduleReconnect();
			}
		}
	});

	SttSocket->OnMessage().AddLambda([WeakThis](const FString& Message)
	{
		if (UGodfreyVoiceInputComponent* Self = WeakThis.Get())
		{
			Self->HandleSocketMessage(Message);
		}
	});

	UE_LOG(LogGodfreyVoiceInput, Log, TEXT("GodfreyVoiceInput: connecting STT websocket %s"), *Url);
	SttSocket->Connect();
}

void UGodfreyVoiceInputComponent::DisconnectSttWebSocket()
{
	if (!SttSocket.IsValid())
	{
		return;
	}

	SttSocket->Close();
	SttSocket.Reset();
	bServerReady = false;
}

void UGodfreyVoiceInputComponent::SendControl(const FString& Action)
{
	if (!SttSocket.IsValid() || !SttSocket->IsConnected())
	{
		return;
	}

	const FString Payload = FString::Printf(TEXT("{\"type\":\"control\",\"action\":\"%s\"}"), *Action);
	SttSocket->Send(Payload);
}

void UGodfreyVoiceInputComponent::HandleSocketMessage(const FString& Message)
{
	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return;
	}

	FString Type;
	if (!Json->TryGetStringField(TEXT("type"), Type))
	{
		return;
	}

	if (Type == TEXT("ready"))
	{
		bServerReady = true;
		FString ModelName;
		Json->TryGetStringField(TEXT("model"), ModelName);
		UE_LOG(LogGodfreyVoiceInput, Log, TEXT("GodfreyVoiceInput: Brain STT ready (model=%s)."), *ModelName);
		if (bMicPaused)
		{
			SendControl(TEXT("pause"));
		}
		return;
	}

	if (Type == TEXT("speech_started"))
	{
		bSpeechActive = true;
		ArmAwaitingFinalTranscript();
		OnSpeechStarted.Broadcast();
		if (AActor* const Character = GodfreyVoiceInputPrivate::ResolveGodfreyCharacter(this))
		{
			if (UGodfreyPerformanceStateComponent* Perf =
				Character->FindComponentByClass<UGodfreyPerformanceStateComponent>())
			{
				Perf->NotifyVisitorSpeechBegan();
			}
		}
		return;
	}

	if (Type == TEXT("speech_stopped"))
	{
		bSpeechActive = false;
		ScheduleMissedTranscriptWatch();
		OnSpeechStopped.Broadcast();
		if (AActor* const Character = GodfreyVoiceInputPrivate::ResolveGodfreyCharacter(this))
		{
			if (UGodfreyPerformanceStateComponent* Perf =
				Character->FindComponentByClass<UGodfreyPerformanceStateComponent>())
			{
				Perf->NotifyVisitorSpeechEnded();
			}
		}
		return;
	}

	if (Type == TEXT("transcript_delta"))
	{
		FString Delta;
		Json->TryGetStringField(TEXT("delta"), Delta);
		if (!Delta.IsEmpty())
		{
			OnTranscriptDelta.Broadcast(Delta);
		}
		return;
	}

	if (Type == TEXT("transcript_completed"))
	{
		FString Transcript;
		Json->TryGetStringField(TEXT("transcript"), Transcript);
		Transcript = Transcript.TrimStartAndEnd();
		OnTranscriptCompleted.Broadcast(Transcript);
		HandleFinalTranscript(Transcript);
		return;
	}

	if (Type == TEXT("transcript_missed"))
	{
		FString Reason;
		Json->TryGetStringField(TEXT("reason"), Reason);
		HandleTranscriptMissed(Reason.IsEmpty() ? TEXT("unknown") : Reason);
		return;
	}

	if (Type == TEXT("error"))
	{
		FString Err;
		Json->TryGetStringField(TEXT("error"), Err);
		if (Err.IsEmpty())
		{
			Err = TEXT("Unknown STT error");
		}
		UE_LOG(LogGodfreyVoiceInput, Error, TEXT("GodfreyVoiceInput STT error: %s"), *Err);
		OnVoiceInputError.Broadcast(Err);
	}
}

void UGodfreyVoiceInputComponent::ArmAwaitingFinalTranscript()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MissedTranscriptTimerHandle);
	}
	bAwaitingFinalTranscript = true;
}

void UGodfreyVoiceInputComponent::ClearAwaitingFinalTranscript()
{
	bAwaitingFinalTranscript = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MissedTranscriptTimerHandle);
	}
}

void UGodfreyVoiceInputComponent::ScheduleMissedTranscriptWatch()
{
	if (!bEnableMissedTranscriptPrompt || !bAwaitingFinalTranscript)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(MissedTranscriptTimerHandle);
	TWeakObjectPtr<UGodfreyVoiceInputComponent> WeakThis(this);
	World->GetTimerManager().SetTimer(
		MissedTranscriptTimerHandle,
		FTimerDelegate::CreateLambda([WeakThis]()
		{
			if (UGodfreyVoiceInputComponent* Self = WeakThis.Get())
			{
				Self->HandleTranscriptMissed(TEXT("timeout"));
			}
		}),
		FMath::Max(0.5f, MissedTranscriptTimeoutSeconds),
		false);
}

void UGodfreyVoiceInputComponent::HandleTranscriptMissed(const FString& Reason)
{
	if (!bAwaitingFinalTranscript)
	{
		return;
	}
	if (Reason.Equals(TEXT("hallucination"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogGodfreyVoiceInput, Log,
			TEXT("GodfreyVoiceInput: dropped STT noise-hallucination (no please-repeat)"));
		ClearAwaitingFinalTranscript();
		return;
	}
	TryFireMissedTranscriptPrompt(Reason);
}

bool UGodfreyVoiceInputComponent::TryFireMissedTranscriptPrompt(const FString& Reason)
{
	if (!bEnableMissedTranscriptPrompt || !bAwaitingFinalTranscript)
	{
		return false;
	}

	ClearAwaitingFinalTranscript();

	if (IsGodfreySpeaking() || IsInPostSpeechIgnore())
	{
		UE_LOG(LogGodfreyVoiceInput, Log,
			TEXT("GodfreyVoiceInput: skipped missed-transcript prompt (Godfrey busy) reason=%s"), *Reason);
		return false;
	}

	const FString Prompt = MissedTranscriptPrompt.TrimStartAndEnd();
	if (Prompt.IsEmpty())
	{
		return false;
	}

	EnsureDirectSpeech();
	if (!IsValid(DirectSpeech) || DirectSpeech->IsStreaming())
	{
		UE_LOG(LogGodfreyVoiceInput, Warning,
			TEXT("GodfreyVoiceInput: missed-transcript AskGodfrey unavailable reason=%s"), *Reason);
		return false;
	}

	UE_LOG(LogGodfreyVoiceInput, Log,
		TEXT("GodfreyVoiceInput: missed transcript → AskGodfrey please-repeat (reason=%s)"), *Reason);
	SetMicPaused(true);
	if (AActor* const Character = GodfreyVoiceInputPrivate::ResolveGodfreyCharacter(this))
	{
		if (UGodfreyPerformanceStateComponent* Perf =
			Character->FindComponentByClass<UGodfreyPerformanceStateComponent>())
		{
			Perf->NotifyVisitorSpoke();
		}
	}
	if (!DirectSpeech->AskGodfrey(Prompt))
	{
		SetMicPaused(false);
		UE_LOG(LogGodfreyVoiceInput, Warning,
			TEXT("GodfreyVoiceInput: missed-transcript AskGodfrey rejected (reason=%s)"), *Reason);
		return false;
	}
	return true;
}

void UGodfreyVoiceInputComponent::HandleFinalTranscript(const FString& Transcript)
{
	if (Transcript.Len() < MinTranscriptChars)
	{
		UE_LOG(LogGodfreyVoiceInput, Verbose,
			TEXT("GodfreyVoiceInput: ignoring short transcript '%s'"), *Transcript);
		HandleTranscriptMissed(TEXT("too_short"));
		return;
	}

	if (GodfreyVoiceInputPrivate::LooksLikeSttPromptEcho(Transcript))
	{
		UE_LOG(LogGodfreyVoiceInput, Warning,
			TEXT("GodfreyVoiceInput: dropping STT prompt-echo transcript '%s'"), *Transcript);
		HandleTranscriptMissed(TEXT("prompt_echo"));
		return;
	}

	if (GodfreyVoiceInputPrivate::LooksLikeSttNoiseHallucination(Transcript))
	{
		UE_LOG(LogGodfreyVoiceInput, Warning,
			TEXT("GodfreyVoiceInput: dropping STT noise-hallucination transcript '%s'"), *Transcript);
		HandleTranscriptMissed(TEXT("hallucination"));
		return;
	}

	const bool bFarewell = GodfreyVoiceInputPrivate::LooksLikeVisitorFarewell(Transcript);
	if (IsGodfreySpeaking() || IsInPostSpeechIgnore())
	{
		if (bFarewell)
		{
			UE_LOG(LogGodfreyVoiceInput, Log,
				TEXT("GodfreyVoiceInput: queueing farewell transcript until Godfrey finishes: %s"), *Transcript);
			ClearAwaitingFinalTranscript();
			QueueOrSendVisitorTranscript(Transcript, true);
			return;
		}
		UE_LOG(LogGodfreyVoiceInput, Log,
			TEXT("GodfreyVoiceInput: dropping transcript while Godfrey is speaking: %s"), *Transcript);
		ClearAwaitingFinalTranscript();
		return;
	}

	ClearAwaitingFinalTranscript();
	QueueOrSendVisitorTranscript(Transcript, bFarewell);
}

void UGodfreyVoiceInputComponent::QueueOrSendVisitorTranscript(const FString& Transcript, const bool bIsFarewell)
{
	if (bIsFarewell && (IsGodfreySpeaking() || (IsValid(DirectSpeech) && DirectSpeech->IsStreaming())))
	{
		PendingFarewellTranscript = Transcript;
		UE_LOG(LogGodfreyVoiceInput, Log,
			TEXT("GodfreyVoiceInput: farewell pending until current speech ends: %s"), *Transcript);
		return;
	}

	EnsureDirectSpeech();
	if (!IsValid(DirectSpeech))
	{
		const FString Err = TEXT("GodfreyVoiceInput: transcript ready but DirectSpeech is missing.");
		OnVoiceInputError.Broadcast(Err);
		return;
	}

	UE_LOG(LogGodfreyVoiceInput, Log, TEXT("GodfreyVoiceInput: AskGodfrey transcript='%s'"), *Transcript);
	SetMicPaused(true);
	if (AActor* const Character = GodfreyVoiceInputPrivate::ResolveGodfreyCharacter(this))
	{
		if (UGodfreyPerformanceStateComponent* Perf =
			Character->FindComponentByClass<UGodfreyPerformanceStateComponent>())
		{
			Perf->NotifyVisitorSpoke();
		}
	}
	if (!DirectSpeech->AskGodfrey(Transcript))
	{
		SetMicPaused(false);
		if (bIsFarewell)
		{
			PendingFarewellTranscript = Transcript;
			UE_LOG(LogGodfreyVoiceInput, Warning,
				TEXT("GodfreyVoiceInput: AskGodfrey rejected farewell — will retry after speech ends."));
			return;
		}
		UE_LOG(LogGodfreyVoiceInput, Warning, TEXT("GodfreyVoiceInput: AskGodfrey rejected transcript."));
	}
}

void UGodfreyVoiceInputComponent::TrySendPendingFarewellTranscript()
{
	if (PendingFarewellTranscript.IsEmpty())
	{
		return;
	}
	if (IsGodfreySpeaking() || (IsValid(DirectSpeech) && DirectSpeech->IsStreaming()))
	{
		return;
	}
	const FString Text = PendingFarewellTranscript;
	PendingFarewellTranscript.Reset();
	UE_LOG(LogGodfreyVoiceInput, Log,
		TEXT("GodfreyVoiceInput: sending queued farewell transcript: %s"), *Text);
	QueueOrSendVisitorTranscript(Text, true);
}

void UGodfreyVoiceInputComponent::ScheduleReconnect()
{
	if (!bWantListening || bReconnectScheduled)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bReconnectScheduled = true;
	World->GetTimerManager().SetTimer(
		ReconnectTimerHandle,
		FTimerDelegate::CreateLambda([this]()
		{
			bReconnectScheduled = false;
			if (!bWantListening)
			{
				return;
			}

			UE_LOG(LogGodfreyVoiceInput, Log, TEXT("GodfreyVoiceInput: attempting reconnect..."));
			const bool bWasListening = bListening;
			if (bWasListening)
			{
				DisconnectSttWebSocket();
				CloseCaptureStream();
				bListening = false;
			}
			StartListening();
		}),
		FMath::Max(0.5f, ReconnectDelaySeconds),
		false);
}
