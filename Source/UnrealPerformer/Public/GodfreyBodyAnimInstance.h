#pragma once

#include "UnrealPerformerApi.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Root.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNodeSpaceConversions.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "BoneControllers/AnimNode_LookAt.h"
#include "GodfreyCoatClearanceAnimNode.h"
#include "GodfreyBodyAnimInstance.generated.h"

class UAnimSequence;

/**
 * Native MetaHuman body anim instance for Godfrey exhibition pass.
 *
 * Graph: looping IdleStanding → DefaultSlot (legs / travel) → UpperBody overlay from spine_01
 *        → optional clamped LookAt on neck_01 toward Exhibit_CineCamera
 *        → coat-clearance IK (hands/sleeves off the jacket) → output.
 * Slot source is never MetaHuman RefPose, so montage blend-out cannot drift into A-pose.
 */
USTRUCT()
struct FGodfreyBodyAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

public:
	FGodfreyBodyAnimInstanceProxy() = default;
	explicit FGodfreyBodyAnimInstanceProxy(UAnimInstance* InAnimInstance);

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void Update(float DeltaSeconds) override;
	virtual FAnimNode_Base* GetCustomRootNode() override;
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

private:
	void ConstructNodes();

	FAnimNode_SequencePlayer_Standalone NeutralStanceNode;
	FAnimNode_Slot DefaultSlotNode;
	FAnimNode_Slot UpperBodySlotNode;
	FAnimNode_LayeredBoneBlend LayeredBlendNode;
	FAnimNode_ConvertLocalToComponentSpace LocalToCSNode;
	FAnimNode_LookAt NeckLookAtNode;
	FAnimNode_GodfreyCoatClearance CoatClearanceNode;
	FAnimNode_ConvertComponentToLocalSpace CSToLocalNode;
	FAnimNode_Root RootNode;
};

UCLASS(BlueprintType, Blueprintable)
class UNREAL_PERFORMER_API UGodfreyBodyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UGodfreyBodyAnimInstance(const FObjectInitializer& ObjectInitializer);

	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	static const FName DefaultBodyMontageSlotName;
	static const FName UpperBodyMontageSlotName;
	static const FName UpperBodyBlendBoneName;
	static const FName NeckAimBoneName;

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Body")
	void SetUpperBodyLayerWeight(float NewWeight);

	UFUNCTION(BlueprintPure, Category = "Godfrey|Body")
	float GetUpperBodyLayerWeight() const { return UpperBodyLayerWeight; }

	void SetNeutralStanceSequence(UAnimSequence* Sequence);
	void EnsureNeutralStanceSequenceLoaded();
	UAnimSequence* GetNeutralStanceSequence() const { return NeutralStanceSequence; }

	void SetConversationHeadAim(bool bEnable, const FVector& WorldTarget, float ClampDegrees, float Alpha);

	bool IsConversationHeadAimEnabled() const { return bHeadAimEnabled; }
	FVector GetHeadAimWorldTarget() const { return HeadAimWorldTarget; }
	float GetHeadAimClampDegrees() const { return HeadAimClampDegrees; }
	float GetHeadAimAlpha() const { return bHeadAimEnabled ? HeadAimAlpha : 0.f; }

private:
	float UpperBodyLayerWeight = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> NeutralStanceSequence;

	bool bHeadAimEnabled = false;
	FVector HeadAimWorldTarget = FVector::ZeroVector;
	float HeadAimClampDegrees = 16.f;
	float HeadAimAlpha = 0.f;

	bool bLoggedActiveSlotWeight = false;
	bool bLoggedHeadAim = false;
	bool bLoggedNeutralStance = false;
};
