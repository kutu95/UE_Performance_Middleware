#pragma once

#include "UnrealPerformerApi.h"
#include "Components/ActorComponent.h"
#include "GodfreyPerformanceTypes.h"
#include "InputCoreTypes.h"
#if WITH_EDITOR
#include "Containers/Ticker.h"
#endif
#include "GodfreyPerformerAnimationBridgeComponent.generated.h"

class UAnimMontage;
class UAnimSequence;
class UChaosClothComponent;
class UDataTable;
class UGodfreyPerformanceStateComponent;
class USkeletalMeshComponent;

/**
 * Godfrey Performer v2/v3 — animation bridge for subtle body presence (MetaHuman Body mesh, upper-body layered montages).
 *
 * Subscribes to UGodfreyPerformanceStateComponent on the same actor. v3 adds: montage deduplication, emphasis cooldown,
 * optional looping idle-breath montage, read-only idle oscillators for AnimBP wiring, and optional soft actor yaw toward
 * an attention target (no IK, no Control Rig, no face graph changes). ACE / A2F / streaming stay on existing paths.
 *
 * Montage setup: DefaultSlot is planted stance (or travel/full-body). Conversation/idle AS play
 * on UpperBody (spine and up) so blends cannot skate the legs. Assign only TargetSkeletalMesh
 * to the body mesh — do not drive Face mesh montages from here.
 */
UCLASS(ClassGroup = (Godfrey), meta = (BlueprintSpawnableComponent))
class UNREAL_PERFORMER_API UGodfreyPerformerAnimationBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGodfreyPerformerAnimationBridgeComponent();

	virtual void InitializeComponent() override;
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	bool EditorShirtDiagnosticTickerPoll(float DeltaTime);
	void EnsureEditorShirtDiagnosticTicker();
	void RemoveEditorShirtDiagnosticTicker();
	FTSTicker::FDelegateHandle EditorShirtDiagnosticTickerHandle;
#endif
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Montage playback (safe no-ops when mesh / AnimInstance / montage missing) ---

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlayListeningBehaviour();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlayThinkingBehaviour();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlaySpeakingStartBehaviour();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlaySpeakingIdleBehaviour();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlayEmphasisBehaviour();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlayAmusedBehaviour();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlaySeriousBehaviour();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlayReturnToIdleBehaviour();

	/** Stops SpeakingIdleMontage on the target mesh if it is currently active (blend 0.25s). */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void StopSpeakingBehaviour();

	/** Call after toggling bEnableIdleMicroMotion / bEnableAttentionTargetFollow at runtime so tick starts/stops. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void RefreshBehaviourTick();

	/**
	 * Play a named library performance (CueId e.g. TwoThumbsUp_01 or AS_TwoThumbsUp_01).
	 * Resolves optional PerformanceActionTable, then AM_/AS_ under PerformanceLibraryPath.
	 * bIgnorePresenceLock: operator / advertising playback — plays during SeaIdle (otherwise named
	 * actions are reserved for Conversing so they cannot interrupt look-to-sea / engage / farewell).
	 */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|Library")
	bool PlayNamedPerformanceAction(const FString& CueId, bool bInterruptSpeakingIdle = true,
		bool bIgnorePresenceLock = false);

	/**
	 * Operator/test capture: suspend interactive exhibit systems, optionally start Take Recorder,
	 * play a library AS once (body montage + Face CTRL curves), stop/save the take, then resume.
	 * Does not pause the world clock (that would freeze animation and the recorder).
	 */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|Library")
	bool PlayOperatorPerformanceClip(const FString& CueId);

	/** Loop look-to-sea idle (exhibition presence SeaIdle). */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|Presence")
	void PlaySeaIdleLoop();

	/** Turn to visitor then greeting; on finish notifies PerformanceState EngageSequenceFinished. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|Presence")
	void PlayEngageSequence();

	/** Farewell wave then turn back to sea; on finish notifies FarewellSequenceFinished. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|Presence")
	void PlayFarewellSequence();

	/** Current body AnimSequence asset name (AS_*) for the on-screen debug overlay. */
	UFUNCTION(BlueprintPure, Category = "Godfrey|Performer|Bridge|Debug")
	FString GetDebugPlayingSequenceName() const;

	/** Play context for the overlay (SpeakingIdle, SeaIdle, Listening, …). */
	UFUNCTION(BlueprintPure, Category = "Godfrey|Performer|Bridge|Debug")
	FString GetDebugPlayingContextName() const;

	/** If TargetSkeletalMesh is unset, find owner mesh whose name contains BodyMeshNameHint and not FaceMeshNameExclude. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	bool ResolveTargetBodyMesh();

	/** Sets CurrentAttentionTarget and logs; intended for visitor / prop tracking (soft yaw only). */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|Attention")
	void SetCurrentAttentionTarget(AActor* NewTarget);

	/** Logs visibility + leader-pose state for every owner skeletal mesh (Body vs Torso/Legs/Feet). */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|MeshPropagation")
	void LogSkeletalMeshPropagationReport() const;

	/** Wire Torso/Legs/Feet (and other followers) to copy bone pose from the resolved Body mesh. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|MeshPropagation")
	int32 WireClothingMeshesToBodyLeaderPose();

	/** Debug: hide clothing meshes and/or force the Body mesh visible to isolate whether Body is animating. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|MeshPropagation|Debug")
	void ApplyBodyMotionDebugVisibility(bool bHideClothingMeshes, bool bForceBodyMeshVisible);

	// --- Animation targets (assign Body / compatible mesh with AnimBP) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge")
	TObjectPtr<USkeletalMeshComponent> TargetSkeletalMesh;

	/** When TargetSkeletalMesh is unset at BeginPlay, auto-pick a body mesh (MetaHuman "Body", not Face). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge")
	bool bAutoResolveMetaHumanBodyMesh = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge")
	FString BodyMeshNameHint = TEXT("Body");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge")
	FString FaceMeshNameExclude = TEXT("Face");

	/** MetaHuman clothing meshes that should copy animated bone pose from Body (leader pose). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation")
	bool bAutoWireClothingLeaderPoseToBody = false;

	/**
	 * When false (default), MetaHumanComponentUE owns Torso/Legs/Feet — bridge only drives Body montages.
	 * Enable only for non-MetaHuman test bodies or after deliberate debugging.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation")
	bool bManageMetaHumanGarmentsAtRuntime = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation")
	TArray<FName> ClothingFollowerMeshNames = { FName(TEXT("Torso")), FName(TEXT("Legs")), FName(TEXT("Feet")) };

	/**
	 * When true, keep coat/tank on the skinned pose (no Chaos Cloth). Overridden at BeginPlay
	 * when GodfreyCoatClothSimulation is on — then the coat sim collides with the body instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation")
	bool bPinClothingToSkinnedPose = true;

	/** Applied at BeginPlay when true — hides Torso/Legs/Feet so only Body is visible for motion diagnosis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation|Debug")
	bool bDebugHideClothingMeshesAtBeginPlay = false;

	/** Applied at BeginPlay when true — forces Body mesh visible even if BP_Gavin hid it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation|Debug")
	bool bDebugForceBodyMeshVisibleAtBeginPlay = false;

	/**
	 * Keep MetaHuman Body rendered (hands/neck). Required for external costumes that have no hand mesh.
	 * Also blocks garment-leader paths from re-hiding Body every tick.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation")
	bool bKeepBodyMeshVisible = false;

	/** Log Torso/Body/CopyPose state when zooming, tick/visibility changes, or bone mismatch (Output Log: ShirtDiag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation|Debug")
	bool bLogMetaHumanShirtDiagnostics = true;

	/** Minimum seconds between periodic shirt diagnostic snapshots (state-change snapshots always log). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation|Debug",
		meta = (ClampMin = "0.05", ClampMax = "5"))
	float ShirtDiagnosticMinLogInterval = 0.25f;

	/** World-space pelvis delta above this (cm) triggers EXPLOSION_SUSPECT warning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation|Debug",
		meta = (ClampMin = "10", ClampMax = "500"))
	float ShirtExplosionPelvisDeltaThresholdCm = 75.f;

	/**
	 * When auto-assigning placeholder montages, prefer large/obvious clips (wave/gesture) over subtle foot/calf loops.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	bool bPreferObviousPlaceholderAnimations = true;

	/**
	 * When montage slots are empty, scan PlaceholderMontageSearchPath for a compatible AnimSequence / AnimMontage
	 * on the body skeleton and build temporary dynamic montages (exhibition placeholder pass).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	bool bAutoAssignPlaceholderMontages = true;

	/** When false, skip all body montage plays (face/ACE still run). Enabled once the AS_* performance library is wired. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	bool bEnableBodyMontages = true;

	/**
	 * When true, BeginPlay sets bEnableBodyMontages after performance-library defaults assign successfully.
	 * Lets older BPs that serialized bEnableBodyMontages=false pick up the library without a manual flag flip.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library")
	bool bEnableBodyMontagesWhenLibraryReady = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	FName PlaceholderMontageSearchPath = FName(TEXT("/Game/Godfrey/Animation/Animation/Performances"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	FName PlaceholderMontageSlotName = FName(TEXT("UpperBody"));

	/** When true, rebuild montages missing the target slot track at runtime (CreateSlotAnimationAsDynamicMontage). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	bool bAutoRemapMontagesToBodySlot = true;

	/**
	 * When montage slots are empty, load default AS_* clips from PerformanceLibraryPath and build DefaultSlot montages.
	 * Prefer authored AM_* assets when present (same stem as AS_*).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library")
	bool bAutoAssignPerformanceLibraryDefaults = true;

	/** Content folder containing AS_* / AM_* performance assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library")
	FName PerformanceLibraryPath = FName(TEXT("/Game/Godfrey/Animation/Animation/Performances"));

	/**
	 * Optional DataTable (row struct FGodfreyPerformanceActionRow) for explicit CueId → clip mapping.
	 * When unset, named actions resolve by asset name under PerformanceLibraryPath.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library")
	TObjectPtr<UDataTable> PerformanceActionTable;

	/** When true, cues with type action/performance/gesture (or AS_/AM_ values) play named library clips. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library")
	bool bPlayNamedActionsFromCueBus = true;

	/**
	 * Brain `[gesture:]` owns UpperBody while that take plays. If Brain sends none, or the take
	 * ends while he is still talking, Unreal plays a shuffled basic explaining pool — it must
	 * not leave Greeting/Thinking/Idle on, and must not loop the same Brain stem.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library")
	bool bBrainOwnsSpeakingBody = true;

	/**
	 * Safety remap for known down-looking / look-away catalog actions.
	 * When true, problematic named actions are overridden to neutral/front-facing alternatives at runtime.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library")
	bool bOverrideDownwardGazeActions = true;

	/** Force broader action remaps to camera-safe stems during conversation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library")
	bool bStrictCameraSafeActionRemap = true;

	/**
	 * Prefer non-destructive AS_*_EyeFixed / AM_*_EyeFixed library variants when present.
	 * Overridden at BeginPlay from Project Settings → Animation|Gaze
	 * (`bGodfreyPreferEyeFixedLibraryVariants`). Currently off while reviewing new takes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library")
	bool bPreferEyeFixedLibraryVariants = false;

	/** PIE K/F7 operator capture. No on-screen hint. Console: godfrey.OperatorCapture */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Operator Capture")
	bool bEnableDebugPerformancePlayKey = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Operator Capture")
	FKey DebugPerformancePlayKey = EKeys::K;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Operator Capture")
	FString DebugPerformancePlayCueId = TEXT("MHP_DuckUnderBanner_01");

	/** Start/stop UE Take Recorder around the operator clip (editor PIE only) and save the take. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Operator Capture")
	bool bCaptureTakeRecorderOnDebugPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Operator Capture",
		meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float OperatorCaptureRecorderWarmupSeconds = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Operator Capture",
		meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float OperatorCaptureRecorderTailSeconds = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> PlaceholderMontageOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> ListeningEnterMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> ThinkingMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> SpeakingStartMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> SpeakingIdleMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> EmphasisMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> AmusedMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> SeriousMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> ReturnToIdleMontage;

	/** Optional subtle loop (additive) played when returning to idle attention; stopped when listening/thinking/speaking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|v3 Idle")
	TObjectPtr<UAnimMontage> IdleBreathingMontage;

	/** Soft defaults used when montage slots are empty (assigned at BeginPlay from PerformanceLibraryPath). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library|Defaults")
	TSoftObjectPtr<UAnimSequence> DefaultListeningSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library|Defaults")
	TSoftObjectPtr<UAnimSequence> DefaultThinkingSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library|Defaults")
	TSoftObjectPtr<UAnimSequence> DefaultSpeakingStartSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library|Defaults")
	TSoftObjectPtr<UAnimSequence> DefaultSpeakingIdleSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library|Defaults")
	TSoftObjectPtr<UAnimSequence> DefaultEmphasisSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library|Defaults")
	TSoftObjectPtr<UAnimSequence> DefaultAmusedSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library|Defaults")
	TSoftObjectPtr<UAnimSequence> DefaultSeriousSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library|Defaults")
	TSoftObjectPtr<UAnimSequence> DefaultReturnToIdleSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library|Defaults")
	TSoftObjectPtr<UAnimSequence> DefaultIdleBreathingSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Presence")
	TObjectPtr<UAnimMontage> SeaIdleMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Presence")
	TObjectPtr<UAnimMontage> EngageTurnMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Presence")
	TObjectPtr<UAnimMontage> EngageGreetMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Presence")
	TObjectPtr<UAnimMontage> FarewellWaveMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Presence")
	TObjectPtr<UAnimMontage> BackToSeaMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library|Defaults")
	TSoftObjectPtr<UAnimSequence> DefaultSeaIdleSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library|Defaults")
	TSoftObjectPtr<UAnimSequence> DefaultEngageTurnSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library|Defaults")
	TSoftObjectPtr<UAnimSequence> DefaultEngageGreetSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library|Defaults")
	TSoftObjectPtr<UAnimSequence> DefaultFarewellWaveSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Library|Defaults")
	TSoftObjectPtr<UAnimSequence> DefaultBackToSeaSequence;

	/** When true, bridge listens to exhibition presence events (sea / engage / farewell). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Presence")
	bool bDriveExhibitionPresenceMontages = true;

	/**
	 * Skip AS_GreetingTurnToVisitor during engage. Exhibit_CineCamera is already in front of sea-idle face;
	 * the turn clip rotates him into profile even when actor yaw is correct.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Presence")
	bool bSkipEngageTurnMontage = true;

	/**
	 * Skip AS_GreetingWelcome on speech-driven engage (R2). Welcome is deferred until visitor
	 * presence detection exists — do not rely on Brain short-hello cues (too much lag).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Presence")
	bool bSkipEngageGreetMontage = true;

	/**
	 * Arm once before NotifyVisitorEngaged so presence-driven engage plays Welcome even when
	 * bSkipEngageGreetMontage stays true for the speech path (R2 / R17).
	 */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|Presence")
	void ArmPresenceWelcomeEngage();

	/** Keep chaining Greeting* until the arrival card finishes (Godfrey is visible behind the text). */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|Presence")
	void SetHoldEngageGreeting(bool bHold);

	/**
	 * Pool for visitor-listen / in-dialog idle posture (R3/R9). Stems without AS_ prefix; EyeFixed preferred.
	 * Empty → built-in Attentive/Curious/Concerned/Nodding. Played as a shuffled non-repeating deck.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Pools")
	TArray<FString> ListeningWhileVisitorSpeaksPool;

	/** Last stem played from the listening pool (avoid immediate reshuffle collision). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|Montages|Pools", Transient)
	FString LastListeningPoolStem;

	/** Shuffled order of listening-pool stems; consumed then reshuffled (R9). */
	UPROPERTY(Transient)
	TArray<FString> ShuffledListeningPoolOrder;

	UPROPERTY(Transient)
	int32 NextListeningPoolIndex = 0;

	/**
	 * Pool while the lantern is Wait / Brain is generating (R3). Camera-safe Thinking* only.
	 * Empty → HandToChin / Thinking_01/02 / DeepBreath / Remembering. Coy and ScratchingHead are rare (≈1 in 8 decks).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Pools")
	TArray<FString> ThinkingWhileAwaitingBrainPool;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|Montages|Pools", Transient)
	FString LastThinkingPoolStem;

	UPROPERTY(Transient)
	TArray<FString> ShuffledThinkingPoolOrder;

	UPROPERTY(Transient)
	int32 NextThinkingPoolIndex = 0;

	/**
	 * Pool for out-of-dialog exhibition SeaIdle (R13). Stems without AS_ prefix; EyeFixed preferred.
	 * Empty → built-in look-to-sea / calm standing set. Played as a shuffled non-repeating deck with soft blends.
	 * Never section-loop a single AS.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Pools")
	TArray<FString> SeaIdleExhibitionPool;

	/** Last stem played from the sea-idle pool (avoid immediate reshuffle collision). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|Montages|Pools", Transient)
	FString LastSeaIdlePoolStem;

	/** Shuffled order of sea-idle pool stems; consumed then reshuffled (R13). */
	UPROPERTY(Transient)
	TArray<FString> ShuffledSeaIdlePoolOrder;

	UPROPERTY(Transient)
	int32 NextSeaIdlePoolIndex = 0;

	/**
	 * Pool for body motion while speaking (R14). Stems without AS_ prefix; EyeFixed preferred.
	 * Unused while bBrainOwnsSpeakingBody (default). Legacy fallback only if that flag is off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Pools")
	TArray<FString> SpeakingIdlePool;

	/** Last stem played from the speaking pool (avoid immediate reshuffle collision). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|Montages|Pools", Transient)
	FString LastSpeakingPoolStem;

	/** Shuffled order of speaking-pool stems; consumed then reshuffled (R14). */
	UPROPERTY(Transient)
	TArray<FString> ShuffledSpeakingPoolOrder;

	UPROPERTY(Transient)
	int32 NextSpeakingPoolIndex = 0;

	/**
	 * Pool for the first in-dialog body hold after SeaIdle → dialog (R15). Prefer Greeting* over Listening*.
	 * Empty → Welcome_02/03 (priority) then Welcome_01 / Nod / SmallSmile / HaveASeat.
	 * Used once per encounter; subsequent dialog idles use ListeningWhileVisitorSpeaksPool (R9).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|Pools")
	TArray<FString> DialogGreetingPool;

	/** Last stem played from the dialog-greeting pool. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|Montages|Pools", Transient)
	FString LastDialogGreetingStem;

	UPROPERTY(Transient)
	TArray<FString> ShuffledDialogGreetingPoolOrder;

	UPROPERTY(Transient)
	int32 NextDialogGreetingPoolIndex = 0;

	/** True after the first dialog greeting hold this encounter (reset on SeaIdle). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|Montages|Pools", Transient)
	bool bUsedFirstDialogGreetingHold = false;

	// --- v3 tuning (AnimBP + montage pacing) ---

	/** Scales play rate for speaking start / idle montages when restarted (1 = default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3", meta = (ClampMin = "0.25", ClampMax = "2.5"))
	float SpeakingMotionIntensity = 0.9f;

	/**
	 * Legacy: section-loop a single SpeakingIdleMontage. Ignored when SpeakingIdlePool has (or defaults to) clips —
	 * R14 always chains shuffled one-shots instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3")
	bool bLoopSpeakingIdleMontage = false;

	/** When true (default), skip SpeakingStart if unset or same as idle — go straight into the speaking-pool chain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3")
	bool bPreferSpeakingIdleLoopOnly = true;

	/** Multiplier for idle micro-motion oscillators (AnimBP curves). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3", meta = (ClampMin = "0", ClampMax = "3"))
	float IdleBreathingIntensity = 1.f;

	/** Max yaw delta (degrees) applied toward CurrentAttentionTarget from the cached exhibit yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention", meta = (ClampMin = "0", ClampMax = "45"))
	float AttentionOffsetStrength = 16.f;

	/** Minimum seconds between emphasis montage plays (delegate still fires; see bFireBridgeEmphasisOnCooldownSkip). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3", meta = (ClampMin = "0", ClampMax = "10"))
	float GestureCooldownSeconds = 0.85f;

	/** If true, OnBridgeEmphasis still broadcasts when emphasis montage is skipped by cooldown. If false, cooldown is fully silent to Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3")
	bool bFireBridgeEmphasisOnCooldownSkip = false;

	/** When true, short listening/thinking/return montages are not restarted if already playing on the mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3")
	bool bDeduplicateActiveMontagePlays = true;

	/** Updates IdleBreathingWave / IdlePostureSwayWave each tick for AnimBP (no mesh writes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3")
	bool bEnableIdleMicroMotion = false;

	/** Play rate for exhibition sea-idle one-shots (lower = calmer, less restless shifting). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Presence", meta = (ClampMin = "0.25", ClampMax = "1.5"))
	float SeaIdleMontagePlayRate = 0.72f;

	/** Soft actor yaw toward CurrentAttentionTarget; best for stationary exhibit roots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3")
	bool bEnableAttentionTargetFollow = false;

	/** While speaking, temporarily bias yaw toward the active player/camera target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	bool bAutoFocusCameraWhileSpeaking = true;

	/** Max yaw offset while bAutoFocusCameraWhileSpeaking is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention", meta = (ClampMin = "0", ClampMax = "45"))
	float SpeakingCameraAttentionOffsetStrength = 34.f;

	/** Interp speed while bAutoFocusCameraWhileSpeaking is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention", meta = (ClampMin = "0.1", ClampMax = "20"))
	float SpeakingCameraAttentionInterpSpeed = 6.f;

	/** Keep body/root facing the exhibit camera through conversational states. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	bool bStrictConversationCameraFacing = true;

	/** After speech ends, suppress rapid listen/think sequence swaps for a brief settle window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3", meta = (ClampMin = "0.0", ClampMax = "6.0"))
	float PostSpeechSettleSeconds = 2.5f;

	/**
	 * Keep the current speaking body AS playing this many seconds after audible speech ends (lipsync already stopped).
	 * Then soft-blend into Listening* (R16). Interrupted immediately if a new utterance starts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float PostSpeechSpeakingHoldSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	TObjectPtr<AActor> CurrentAttentionTarget;

	/** Optional explicit focus target used for speaking/awaiting-reply orientation (no automatic camera fallback when set). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	TObjectPtr<AActor> ExplicitFocusTargetActor;

	/** Optional runtime lookup by exact actor name (e.g. Exhibit_CineCamera) for level-instance cameras. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	FName ExplicitFocusTargetActorName = NAME_None;

	/** Optional runtime lookup by actor tag (recommended: GodfreyExhibitCamera on Exhibit_CineCamera). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	FName ExplicitFocusTargetActorTag = FName(TEXT("GodfreyExhibitCamera"));

	/** Optional runtime lookup by placed actor label (e.g. Exhibit_CineCamera). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	FString ExplicitFocusTargetActorLabel = TEXT("Exhibit_CineCamera");

	/** Z offset from actor origin used for look-at facing (cm; ~eye height). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention", meta = (ClampMin = "0", ClampMax = "200"))
	float ExhibitionFacingOriginHeightOffset = 60.f;

	/**
	 * Face the runtime exhibition camera using FindLookAtRotation (character -> lens).
	 * When false, uses opposite of camera forward vector instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	bool bFaceCameraViewDirection = false;

	/**
	 * When true, uses PlayerCameraManager. Leave false — runtime view comes from SetViewTarget(Exhibit_CineCamera).
	 * Editor design viewport != runtime camera; PCM after SetViewTarget matches the cine camera.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	bool bPreferPlayerCameraForExhibitionFacing = false;

	/** When true, keep body at sea-idle yaw instead of turning toward the runtime camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	bool bLockBodyYawToExhibitFacingDuringConversation = false;

	/**
	 * Once per PIE: measure look-at yaw to Exhibit_CineCamera and set ExhibitionBodyYawOffsetDegrees so that
	 * sea-idle actor yaw already "faces the lens" (MetaHuman visual face != actor +X).
	 * Logs proved: cam at +Y => rawLookAt=90, correct visual at actorYaw=0 => offset=-90.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	bool bCalibrateFacingOffsetFromSeaIdlePlacement = true;

	/** Subtract mesh component forward vs actor yaw (usually 0 on MetaHuman; prefer sea-idle calibration). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	bool bAutoApplyMeshRelativeYawOffset = false;

	/** When not locked to exhibit yaw, max body turn (degrees) from CachedExhibitYaw toward the viewer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention", meta = (ClampMin = "0", ClampMax = "180"))
	float MaxExhibitionFacingDeltaFromSeaIdle = 25.f;

	/** Extra yaw added after facing solve (auto-filled by sea-idle calibration when enabled). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention", meta = (ClampMin = "-180", ClampMax = "180"))
	float ExhibitionBodyYawOffsetDegrees = 0.f;

	/** Body head LookAt is the conversation neck aim on GodfreyBodyAnimInstance (R21). Keep this false — Face post-process is still unsafe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	bool bEnableVisitorHeadLookAt = false;

	/**
	 * DISABLED for MetaHuman safety. Face post-process override breaks body/face attach and ACE lipsync.
	 * Keep false. Gaze must not replace Face_AnimBP / Face post-process until a proven MetaHuman-safe path exists.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	bool bEnableVisitorEyeLookAt = false;

	/** Head LookAt blend weight while active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention", meta = (ClampMin = "0", ClampMax = "1"))
	float VisitorHeadLookAtAlpha = 1.f;

	/** Face eye LookAt blend weight while active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention", meta = (ClampMin = "0", ClampMax = "1"))
	float VisitorEyeLookAtAlpha = 1.f;

	/** Stop any active body montage before starting another (prevents stacked/crammed sequences). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	bool bEnforceSingleActiveBodyMontage = true;

	/** Interpolation speed for attention yaw (higher = snappier). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention", meta = (ClampMin = "0.1", ClampMax = "20"))
	float AttentionInterpSpeed = 3.f;

	// --- Mirrored state (updated from performer events; for AnimBP / UI reads) ---

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State")
	bool bIsListening = false;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State")
	bool bIsThinking = false;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State")
	bool bIsSpeaking = false;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State")
	bool bIsSerious = false;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State")
	bool bIsAmused = false;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State")
	EGodfreyPerformanceState CurrentPerformanceState = EGodfreyPerformanceState::Idle;

	/** Simple scalar; emphasis bumps it when not on cooldown. */
	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State")
	float GestureIntensity = 1.f;

	/** ~sin wave for subtle breathing drive in AnimBP (updated when bEnableIdleMicroMotion). */
	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State|v3 Idle")
	float IdleBreathingWave = 0.f;

	/** Secondary wave for posture sway / weight shift blend in AnimBP. */
	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State|v3 Idle")
	float IdlePostureSwayWave = 0.f;

	// --- Bridge delegates (secondary event bus for animation Blueprints) ---

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeListening;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeThinking;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeSpeakingStarted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeSpeakingEnded;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeReturnedToIdle;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeEmphasis;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeAmused;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeSerious;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerCueEvent OnBridgeCueReceived;

	/** World yaw for facing the exhibition camera lens (includes ExhibitionBodyYawOffsetDegrees). */
	bool TryGetExhibitionFacingYaw(float& OutYawDegrees);
	float GetCachedExhibitYawDegrees() const { return CachedExhibitYawDegrees; }
	bool HasCachedExhibitYaw() const { return bHasCachedExhibitYaw; }
	float GetCachedMeshVisualYawOffsetDegrees() const { return CachedMeshVisualYawOffsetDegrees; }

private:
	void TryBindPerformerState();
	void UnbindPerformerState();
	void RefreshMirroredPerformanceState();
	void UpdatePerformerTickEnabled();

	bool PlayMontageIfPossible(UAnimMontage* Montage, const TCHAR* ContextLabel, float PlayRate = 1.f,
		bool bRestartIfAlreadyPlaying = true, bool bLoopMontage = false, bool bChainAsHold = false,
		bool bSoftSlotReplace = false, bool bApplyRootMotion = false);

	UAnimInstance* ResolveAnimInstance(const TCHAR* ContextLabel) const;

	void StopIdleBreathingMontageIfActive();
	void TryStartIdleBreathingMontage();
	void PlaySpeakingIdleInternal(bool bRestartIfAlreadyPlaying);

	void LogMontageSetupStatus() const;
	void LogActingCue(const FString& CueType, const FString& CueValue, const FString& RawCue) const;
	void LogActingPlay(const TCHAR* ContextLabel, const UAnimMontage* Montage, const UAnimSequence* Sequence,
		float PlayRate, bool bLoop, float WallLenSeconds, bool bPlayed) const;
	bool ShouldAutoResolveBodyMesh() const;
	void InitAssignedBodyAnimClassOnly();
	void EnsureMontageAnimInstanceReady();
	/** Keep MetaHuman Body cinematic post-process enabled for garment leader pose. */
	void EnsureBodyMontagePlaybackReady();
	void StabilizeClothingLeaderPoseMeshes();
	void TryStabilizeClothingForEditorViewport();
	/** False while sibling Body/Torso/Legs/Feet are still registering — avoid TickComponent before bRegistered. */
	bool AreMetaHumanGarmentMeshesRegistered() const;
	void DeferredClothingStabilize();
	void RefreshClothingPoseAfterStabilize();
	void MaintainClothingLeaderPose();
	bool IsBodyMontagePlaying() const;
	/** True when owner has UMetaHumanComponentUE — stock MetaHuman owns clothing tick / leader pose. */
	bool UsesMetaHumanNativeClothingPipeline() const;
	/** Clothing leader-pose hacks only for non-MetaHuman performers (e.g. mismatched Godfrey test body). */
	bool ShouldManageClothingLeaderPose() const;
	bool IsEditorViewportWorld() const;
	/** True when bridge may alter MetaHuman garment meshes (tick, visibility, leader pose). */
	bool ShouldManageMetaHumanGarmentsAtRuntime() const;
	/** False when bAutoActivate is off — no editor LOD pin, garment refresh, or mesh overrides. */
	bool IsBridgeActiveForMetaHumanIntervention() const;
	/** Torso/shirt (or any follower) uses mesh post-process CopyPoseFromMesh — needs ticking Body source. */
	bool HasMetaHumanGarmentPostProcessMesh() const;
	/** Hidden Body must always tick so garment post-process CopyPoseFromMesh has fresh bones when Torso zooms in. */
	void EnsureMetaHumanBodyTicksForClothingPostProcess();
	void EnsureMetaHumanCopyPoseBodySource();
	void MaintainMetaHumanBodyTickForClothing();
	void MaintainMetaHumanCopyPoseBodySource();
	void ApplyMetaHumanClothingTickPrerequisites(USkeletalMeshComponent* Body);
	void WireClothingPostProcessCopyPoseSource(USkeletalMeshComponent* Body, USkeletalMeshComponent* Garment,
		bool bLogWiringResult = true);
	/** Mirror MetaHumanComponentUE::PostConnectAnimBPVariables for garment post-process (editor has no BeginPlay). */
	void ApplyMetaHumanGarmentPostProcessVariables(USkeletalMeshComponent* Garment, UAnimInstance* PostProcessInstance);
	/** MetaHuman shirt PP uses CopyPose bUseAttachedParent when Source Mesh Component is unset — Torso must attach to Body. */
	void EnsureMetaHumanCopyPoseGarmentAttachedToBody(USkeletalMeshComponent* Body, USkeletalMeshComponent* Garment);
	void RefreshMetaHumanGarmentPoses(USkeletalMeshComponent* Body);
	void EnsureMetaHumanFaceVisible();
	/** After MetaHumanComponentUE::BeginPlay — refresh garment poses without re-InitAnim (preserves CopyPoseFromMesh pins). */
	void DeferredMetaHumanClothingRefresh();
	void ScheduleMetaHumanClothingRefreshPasses();
	void PinClothingToSkinnedPose();
	void ScheduleClothingSkinnedPosePin();
	void EnableCoatClothSimulation();
	void TickCoatClothEnable();
	void ScheduleCoatClothEnable();
	UChaosClothComponent* EnsureGodfreyCoatClothComponent();
	void ScheduleEditorCopyPoseStabilize();
	void ClearPostProcessGarmentLeaderPose(USkeletalMeshComponent* Garment);
	void MaybeLogMetaHumanShirtDiagnostics(const TCHAR* TriggerReason, bool bForce = false);
	void LogMetaHumanShirtDiagnosticSnapshot(const TCHAR* TriggerReason) const;
#if WITH_EDITOR
	/** Pin LODSync + garment meshes to LOD 0 in editor viewport when garment bridge is off (prevents zoom shirt explosion). */
	void PinEditorViewportMetaHumanLOD();
	void StabilizeEditorTorsoOnViewportChange();
	/** Undo editor-only LOD / leader-pose experiments and restore MetaHuman CopyPose garment path. */
	void RestoreEditorMetaHumanViewportDefaults();
	void RestoreEditorTorsoCopyPoseGarment(USkeletalMeshComponent* Body, USkeletalMeshComponent* Torso);
	void ApplyEditorTorsoCopyPoseOnlyOverrides(UAnimInstance* PostProcessInstance) const;
	void ReapplyEditorGarmentPreviewSettings();
#endif
	bool HasClothingFollowerMeshesOnBody() const;
	bool TryAssignPlaceholderMontages();
	bool TryAssignPerformanceLibraryDefaults();
	UAnimSequence* LoadLibrarySequenceByStem(const FString& AssetStem) const;
	UAnimMontage* LoadLibraryMontageByStem(const FString& AssetStem) const;
	/** If prefer-EyeFixed is on, return AS_*_EyeFixed when it exists; otherwise return InSequence. */
	UAnimSequence* PreferEyeFixedSequence(UAnimSequence* InSequence) const;
	/** If bPreferEyeFixedLibraryVariants, swap to AS_/AM_*_EyeFixed when available. */
	UAnimMontage* RemapMontageToEyeFixedVariant(UAnimMontage* Montage);
	UAnimMontage* ResolveNamedActionMontage(const FString& CueId, bool& bOutInterruptSpeakingIdle);
	static FString NormalizePerformanceCueId(const FString& CueId);
	static void StripEyeFixedSuffix(FString& InOutStem);
	static bool IsNamedActionCueType(const FString& CueType);
	static bool LooksLikeNamedPerformanceId(const FString& Token);
	UAnimMontage* MakeOrGetPlaceholderMontage(UAnimSequence* Sequence, const TCHAR* Label, int32 LoopCount = 1,
		FName SlotName = NAME_None);
	UAnimMontage* ResolveMontageForBodySlot(UAnimMontage* Montage, const TCHAR* ContextLabel);
	UAnimMontage* ResolveMontageForSlot(UAnimMontage* Montage, FName SlotName, const TCHAR* ContextLabel);
	UAnimMontage* ResolveLoopedBodySlotMontage(UAnimMontage* Montage, const TCHAR* ContextLabel);
	UAnimMontage* ResolveLoopedSlotMontage(UAnimMontage* Montage, FName SlotName, const TCHAR* ContextLabel);
	void EnsurePlantedStancePlaying();
	void StopPlantedStance(const TCHAR* Reason);
	void StopTravelRootMotionIfActive(const TCHAR* Reason);
	void SetAnimInstanceIgnoreRootMotion(bool bIgnore);
	void ConfigureSequenceRootHandling(UAnimSequence* Sequence, bool bApplyRootMotion);
	bool ShouldApplyRootMotionForAction(const FString& CueId) const;
	FName GetInPlaceOverlaySlotName() const;
	bool IsFullBodyOverrideContext(const TCHAR* ContextLabel) const;
	void MaintainSpeakingIdleMontage();
	void OnSpeakingIdleMontageEnded(UAnimMontage* EndedMontage, bool bInterrupted);
	void BindSpeakingIdleMontageEndDelegate(UAnimInstance* AnimInst, UAnimMontage* PlayMontage);
	UAnimMontage* ResolvePlaceholderMontageAsset();
	/** Keep listening hold alive while awaiting the visitor (Speak green). */
	void PlayAwaitingConversationHoldMontage(const TCHAR* ContextLabel, bool bPreferListeningEnter);
	void MaintainAwaitingConversationHoldMontage();
	void SuppressSpeakingIdleUntil(double WorldTimeSeconds);
	static bool IsNamedTakeHoldContext(const TCHAR* ContextLabel);
	static bool MontageLooksLikePresenceOrGreeting(const UAnimMontage* Montage);
	bool IsSpeakingIdleSuppressed() const;
	/** True while a Brain story `[gesture:]` owns UpperBody — not Welcome/Greeting/Listening. */
	bool ShouldHoldNamedPerformanceTake() const;
	static bool IsBrainSpeakingBodyAction(const FString& CueId);
	void RememberBrainSpeakingAction(const FString& CueId);
	void ClearBrainSpeakingActionMemory(const TCHAR* Reason);
	bool WasSpeakingStemUsedThisUtterance(const FString& Stem) const;
	void MarkSpeakingStemUsedThisUtterance(const FString& Stem);
	bool TryPlayBrainSpeakingAction(const FString& CueId, const TCHAR* Reason, bool bIgnorePresenceLock);
	/** Play now, stash until speak-start, or queue behind the take already on UpperBody. Never interrupt a live Brain take. */
	bool EnqueueOrPlayBrainSpeakingAction(const FString& CueId, const TCHAR* Reason);
	bool TryPlayNextQueuedBrainSpeakingTake(const TCHAR* Reason);
	bool ApplyPendingBrainSpeakingTake(const TCHAR* Reason);
	bool SustainBrainSpeakingTake(const TCHAR* Reason);
	/** True when EndedMontage is a clip we already replaced (early-chain / same-frame pool). */
	bool IsReplacedOverlayMontage(UAnimMontage* EndedMontage) const;
	void HoldSpeakingPoolForMontage(UAnimMontage* Montage, const TCHAR* Reason);
	void ClearNamedPerformanceTakeHold(const TCHAR* Reason);
	void ConsumeFirstDialogGreetingHold(const TCHAR* Reason);
	void InterruptGreetingOverlayForSpeech();
	/** True while exhibition presence owns the body (SeaIdle / Engaging / Farewell) — suppress story cues. */
	bool ShouldSuppressPresenceOwnedBodyCues() const;
	/** Clear a montage slot when its stem matches (e.g. stale IdleStanding SeaIdle/IdleBreathing). */
	void ClearMontageSlotIfStemEquals(TObjectPtr<UAnimMontage>& Slot, const TCHAR* Stem);

	void PlayPresenceMontageChainStep(UAnimMontage* Montage, const TCHAR* Label, FTimerHandle& TimerHandle,
		FTimerDelegate NextStep, float FallbackSeconds = 2.5f);
	void AdvanceEngageAfterTurn();
	void AdvanceEngageAfterGreet();
	void AdvanceFarewellAfterWave();
	void AdvanceFarewellAfterBackToSea();
	UAnimMontage* ResolvePresenceMontageSlot(TObjectPtr<UAnimMontage>& Slot,
		const TSoftObjectPtr<UAnimSequence>& SoftSeq, const TCHAR* Label, int32 LoopCount = 1);
	/** R3/R9: next listening clip from shuffled deck (EyeFixed when available). */
	UAnimMontage* PickListeningMontageFromPool(const TCHAR* ContextLabel);
	void EnsureDefaultListeningPool();
	void ReshuffleListeningPoolOrder();
	FString TakeNextListeningPoolStem();
	/** R3: thinking-pool one-shots while lantern is Wait / Brain generating. */
	UAnimMontage* PickThinkingMontageFromPool(const TCHAR* ContextLabel);
	void EnsureDefaultThinkingPool();
	void ReshuffleThinkingPoolOrder();
	FString TakeNextThinkingPoolStem();
	void PlayThinkingHoldMontage(const TCHAR* ContextLabel);
	void MaintainThinkingHoldMontage();
	static bool IsDialogIdleHoldContext(const TCHAR* ContextLabel);
	/** R14: next speaking body clip from shuffled deck (EyeFixed when available). */
	UAnimMontage* PickSpeakingMontageFromPool(const TCHAR* ContextLabel);
	void EnsureDefaultSpeakingPool();
	void ReshuffleSpeakingPoolOrder();
	FString TakeNextSpeakingPoolStem();
	bool IsSpeakingPoolActive() const;
	/** R15: next first-dialog greeting clip from shuffled deck. */
	UAnimMontage* PickDialogGreetingMontageFromPool(const TCHAR* ContextLabel);
	void EnsureDefaultDialogGreetingPool();
	void ReshuffleDialogGreetingPoolOrder();
	FString TakeNextDialogGreetingPoolStem();
	void ResetFirstDialogGreetingHold();
	/** Presence Welcome: GreetingWelcome_03 then _01 (_02 shelved — A-pose arms). */
	UAnimMontage* PickPresenceWelcomeMontage();
	/** R13: next exhibition sea-idle clip from shuffled deck (EyeFixed when available). */
	UAnimMontage* PickSeaIdleMontageFromPool(const TCHAR* ContextLabel);
	void EnsureDefaultSeaIdlePool();
	void ReshuffleSeaIdlePoolOrder();
	FString TakeNextSeaIdlePoolStem();
	static bool IsSeaIdleHoldContext(const TCHAR* ContextLabel);
	bool IsSeaIdleChainActive() const;
	/** Soft-advance to next sea-idle AS even if one is already playing (crossfade; avoids RefPose gap). */
	void AdvanceSeaIdleChain();
	/** Fire next AS before current blend-out so DefaultSlot weight never drops to RefPose (A-pose). */
	void ScheduleSeaIdleEarlyChainAdvance();
	void ClearSeaIdleChainTimer();
	/** Soft-advance next speaking-pool AS while still speaking (R14). */
	void AdvanceSpeakingIdleChain();
	void ScheduleSpeakingIdleEarlyChainAdvance();
	void ClearSpeakingIdleChainTimer();
	/** Soft-advance next listening/thinking hold AS (R9 — avoids A-pose). */
	void AdvanceDialogIdleChain();
	void ScheduleDialogIdleEarlyChainAdvance();
	void ClearDialogIdleChainTimer();
	/** After post-speech hold, soft-blend speaking body into Listening* (R16). */
	void FinishPostSpeechSpeakingHold();
	void ClearPostSpeechSpeakingHoldTimer();
	void BeginPostSpeechSpeakingHold();
	/** Deferred BeginPlay kick so Body AnimInstance exists — Godfrey must animate before any visitor speech. */
	void ScheduleEnsurePresenceIdleAtBeginPlay();
	void EnsurePresenceIdlePlaying();

	void PollOperatorCaptureKey();
	FKey ResolveOperatorCaptureKey() const;
	void TryRecoverStuckEngageChain(float DeltaTime);
	void StartOperatorCapture(const FString& CueId);
	void PlayOperatorClipAfterWarmup();
	void FinishOperatorCapture();
	void CancelOperatorCaptureTimers();
	void SuspendInteractiveExhibit();
	void ResumeInteractiveExhibit();
	void BeginFaceCurveOverlay(UAnimSequence* Sequence);
	void TickFaceCurveOverlay();
	void StopFaceCurveOverlay();
	void EnsureBodyAnimBlueprintMode(const TCHAR* Reason);
	bool BeginTakeRecorderIfPossible();
	void EndTakeRecorderIfPossible();

	USkeletalMeshComponent* FindFollowerMeshByComponentName(FName MeshName) const;
	int32 ScoreAnimAssetNameForObviousTest(const FString& AssetName) const;

	void UpdateAttentionRotation(float DeltaTime);
	void UpdateVisitorHeadLookAt();
	bool ShouldForceConversationCameraFacing() const;
	void EnsureFaceEyeLookAtPostProcess();
	void RestoreMetaHumanFacePostProcessIfNeeded();
	void StopAllBodyMontages(const TCHAR* Reason);
	void CacheMeshVisualYawOffset();
	bool EnsureFacingOffsetCalibrated();

	UFUNCTION()
	void HandleListeningStarted();

	UFUNCTION()
	void HandleThinkingStarted();

	UFUNCTION()
	void HandleSpeakingStarted();

	UFUNCTION()
	void HandleSpeakingEnded();

	UFUNCTION()
	void HandleReturnedToIdle();

	UFUNCTION()
	void HandleEmphasisTriggered();

	UFUNCTION()
	void HandleAmusedTriggered();

	UFUNCTION()
	void HandleSeriousTriggered();

	UFUNCTION()
	void HandlePerformanceCueReceived(const FString& CueType, const FString& CueValue, const FString& RawCue);

	UFUNCTION()
	void HandleSeaIdleStarted();

	UFUNCTION()
	void HandleEngageSequenceStarted();

	UFUNCTION()
	void HandleFarewellSequenceStarted();

	UPROPERTY(Transient)
	TObjectPtr<UGodfreyPerformanceStateComponent> PerformerState;

	/** Consumed by AdvanceEngageAfterTurn when presence arms Welcome (R17). */
	bool bForceNextEngageGreetMontage = false;
	/** Arrival card is on screen — keep Greeting* instead of finishing engage. */
	bool bHoldEngageGreeting = false;

	float IdleMicroTimeSeconds = 0.f;
	double LastEmphasisMontageWorldTimeSeconds = -1.e10;
	/** While WorldTime < this, speaking-pool start is deferred (named one-shot gestures). */
	double SuppressSpeakingIdleUntilWorldTime = -1.0;
	/** Playing Brain/mood overlay that owns UpperBody until it ends (R14 named take). */
	TObjectPtr<UAnimMontage> ActiveNamedActionPlayMontage;
	/** Named `[gesture:]` received during Engaging Welcome prefetch — play at BeginSpeaking. */
	FString PendingBrainSpeakingActionId;
	/** Later Brain `[gesture:]` markers waiting until the current take finishes (LLM emits them early). */
	TArray<FString> QueuedBrainSpeakingActionIds;
	/** Last Brain speaking take this utterance — do not loop it; chain a different clip if speech continues. */
	FString LastBrainSpeakingActionId;
	/** CatalogIds already played while this utterance is speaking — never repeat in the same line. */
	TArray<FString> UsedSpeakingStemsThisUtterance;
	/** Last body overlay actually passed to Montage_Play (EngageGreet may not be the speaking-idle pointer). */
	TObjectPtr<UAnimMontage> LastPlayedBodyMontage;
	/** Last performance cue seen by the bridge (for [Acting] play lines). */
	FString LastActingCueType;
	FString LastActingCueValue;
	bool bHasCachedExhibitYaw = false;
	float CachedExhibitYawDegrees = 0.f;
	bool bHasCachedMeshVisualYawOffset = false;
	float CachedMeshVisualYawOffsetDegrees = 0.f;
	bool bFacingOffsetCalibrated = false;
	bool bSavedAttentionFollowForSpeaking = false;
	float SavedAttentionOffsetStrengthForSpeaking = 0.f;
	float SavedAttentionInterpSpeedForSpeaking = 0.f;
	TWeakObjectPtr<AActor> SavedAttentionTargetForSpeaking;
	bool bHoldingCameraFocusWhileAwaitingReply = false;
	bool bSavedAttentionFollowForAwaitingReply = false;
	float SavedAttentionOffsetStrengthForAwaitingReply = 0.f;
	float SavedAttentionInterpSpeedForAwaitingReply = 0.f;
	TWeakObjectPtr<AActor> SavedAttentionTargetForAwaitingReply;
	double PostSpeechSettleUntilWorldTime = -1.0;
	/** True while speaking body AS is intentionally held after audio ended (R16). */
	bool bPostSpeechSpeakingBodyHold = false;
	/** True while the once-per-encounter Greeting* hold is the active dialog body (R15). */
	bool bDialogGreetingHoldActive = false;

	/** Montage timeline length (GetPlayLength) for Montage_GetPosition rewind. */
	float SpeakingIdleMontageCycleSeconds = 0.f;
	/** Wall-clock cycle length from Montage_Play return (GetPlayLength / playRate). */
	float SpeakingIdleMontageWallCycleSeconds = 0.f;
	double SpeakingIdleCycleStartWorldTime = -1.0;
	TObjectPtr<UAnimMontage> ActiveSpeakingIdlePlayMontage;
	TObjectPtr<UAnimMontage> ActivePlantedStanceMontage;
	FString LastDebugPlaySequenceName = TEXT("(none)");
	FString LastDebugPlayContextName;
	TObjectPtr<UAnimMontage> ActiveTravelMontage;
	bool bTravelRootMotionActive = false;
	FOnMontageEnded SpeakingIdleMontageEndedDelegate;
	double LastSpeakingIdleCycleRewindLogTime = -1.e10;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimMontage>> GeneratedPlaceholderMontages;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UAnimMontage>, TObjectPtr<UAnimMontage>> BodySlotRemappedMontages;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UAnimMontage>, TObjectPtr<UAnimMontage>> UpperBodyRemappedMontages;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UAnimMontage>, TObjectPtr<UAnimMontage>> LoopedBodySlotMontages;

	FTimerHandle EngageChainTimerHandle;
	FTimerHandle FarewellChainTimerHandle;
	FTimerHandle PresenceIdleRetryTimerHandle;
	FTimerHandle SeaIdleChainTimerHandle;
	FTimerHandle SpeakingIdleChainTimerHandle;
	FTimerHandle DialogIdleChainTimerHandle;
	FTimerHandle PostSpeechSpeakingHoldTimerHandle;
	FTimerHandle OperatorCaptureWarmupTimerHandle;
	FTimerHandle OperatorCaptureFinishTimerHandle;
	uint8 PresenceIdleEnsureAttempts = 0;

	bool bOperatorPerformanceHold = false;
	float StuckEngageEmptySeconds = 0.f;
	FString PendingOperatorCaptureCueId;
	double OperatorFaceCurveStartWorldTime = -1.0;
	TObjectPtr<UAnimSequence> OperatorFaceCurveSequence;
	TArray<FName> OperatorFaceCurveNames;
	TObjectPtr<UAnimMontage> OperatorFaceSlotMontage;
	bool bOperatorSuspendedVoice = false;
	bool bOperatorSuspendedQueuePoll = false;
	bool bOperatorSavedEngageOnPresence = false;
	bool bOperatorMutedEngageOnPresence = false;
	bool bOperatorTakeRecorderStarted = false;

	bool bMetaHumanClothingTickPrerequisitesApplied = false;
	bool bMetaHumanClothingRefreshPassesScheduled = false;
	bool bEditorCopyPoseStabilizeScheduled = false;
	bool bLoggedEditorViewportLODPin = false;
	uint8 MetaHumanClothingRefreshPassCount = 0;
	uint8 ClothingSkinnedPosePinPassCount = 0;
	uint8 MetaHumanGarmentRegisterWaitFrames = 0;
	FTimerHandle ClothingSkinnedPosePinTimerHandle;
	static constexpr uint8 MetaHumanMaxGarmentRegisterWaitFrames = 30;
	static constexpr uint8 MetaHumanMaxClothingRefreshPasses = 5;

	mutable int32 LastLoggedBodyTickOpt = -1;
	mutable int32 LastLoggedTorsoTickOpt = -1;
	mutable int32 LastLoggedBodyVisible = -1;
	mutable int32 LastLoggedTorsoVisible = -1;
	mutable int32 LastLoggedBodyHiddenInGame = -1;
	mutable int32 LastLoggedBodyLOD = -1;
	mutable int32 LastLoggedTorsoLOD = -1;
	mutable double LastShirtDiagnosticLogTimeSeconds = -1.e10;
	mutable float LastEditorViewDistanceToTorso = -1.f;
	mutable float LastEditorViewFOV = -1.f;
	mutable float LastLoggedPelvisDeltaCm = -1.f;
	mutable bool bLoggedCopyPoseWireForTorso = false;
};
