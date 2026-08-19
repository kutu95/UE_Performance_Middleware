#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GodfreyPerformanceTypes.generated.h"

class UAnimMontage;
class UAnimSequence;

/** High-level behavioural / conversational state for Captain Godfrey performance orchestration (body, gaze, montages — not ACE curves). */
UENUM(BlueprintType)
enum class EGodfreyPerformanceState : uint8
{
	Idle UMETA(DisplayName = "Idle"),
	Listening UMETA(DisplayName = "Listening"),
	Thinking UMETA(DisplayName = "Thinking"),
	Speaking UMETA(DisplayName = "Speaking"),
	Emphasising UMETA(DisplayName = "Emphasising"),
	Serious UMETA(DisplayName = "Serious"),
	Amused UMETA(DisplayName = "Amused"),
};

/** Exhibition presence layer (always-animated sea idle ↔ visitor conversation ↔ farewell). */
UENUM(BlueprintType)
enum class EGodfreyExhibitionPresence : uint8
{
	SeaIdle UMETA(DisplayName = "Sea Idle"),
	Engaging UMETA(DisplayName = "Engaging"),
	Conversing UMETA(DisplayName = "Conversing"),
	Farewell UMETA(DisplayName = "Farewell"),
};

/**
 * Webcam / sensor visitor occupancy (sensing only — distinct from EGodfreyExhibitionPresence).
 * Empty → Approaching (raw occupied, enter dwell) → Present → Leaving (raw empty, leave dwell).
 */
UENUM(BlueprintType)
enum class EGodfreyVisitorSenseState : uint8
{
	Empty UMETA(DisplayName = "Empty"),
	Approaching UMETA(DisplayName = "Approaching"),
	Present UMETA(DisplayName = "Present"),
	Leaving UMETA(DisplayName = "Leaving"),
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FGodfreyExhibitionPresenceChangedEvent,
	EGodfreyExhibitionPresence,
	NewPresence,
	EGodfreyExhibitionPresence,
	PreviousPresence);

/**
 * Optional DataTable row for middleware named performance actions
 * (e.g. CueId = SpeakingDescribeSize_01 → AS_/AM_ under Performances).
 * Row name may also be the CueId when CueId is empty.
 */
USTRUCT(BlueprintType)
struct FGodfreyPerformanceActionRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Middleware action id without required AS_/AM_ prefix (e.g. TwoThumbsUp_01). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performance")
	FName CueId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performance")
	TSoftObjectPtr<UAnimSequence> Sequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performance")
	TSoftObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performance")
	bool bOneShot = true;

	/** When true during Speaking, briefly stop SpeakingIdle so this clip can play, then idle resumes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performance")
	bool bInterruptSpeakingIdle = true;

	/** When true, play full-body on DefaultSlot and apply root motion so authored steps move the actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performance")
	bool bApplyRootMotion = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FGodfreyPerformanceStateChangedEvent,
	EGodfreyPerformanceState,
	NewState,
	EGodfreyPerformanceState,
	PreviousState);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGodfreyUtteranceLifecycleEvent);

/** Zero-parameter moment hooks for montages / AnimBP (listening, thinking, speaking edges, mood pulses). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGodfreyPerformerSimpleEvent);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FGodfreyPerformerCueEvent,
	const FString&,
	CueType,
	const FString&,
	CueValue,
	const FString&,
	RawCue);
