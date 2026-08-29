#pragma once

#include "UnrealPerformerApi.h"
#include "BoneContainer.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "BonePose.h"
#include "GodfreyCoatClearanceAnimNode.generated.h"

/**
 * Keeps Godfrey's hands/sleeves outside the jacket by two-bone IK when a
 * performance pose would drive them through the coat torso or hem.
 */
USTRUCT()
struct FAnimNode_GodfreyCoatClearance : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	FAnimNode_GodfreyCoatClearance();

	virtual void EvaluateSkeletalControl_AnyThread(
		FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;

	bool bEnabled = true;
	float MinHandLateralCm = 18.f;
	float MinElbowLateralCm = 22.f;
	float ForwardStartCm = 6.f;
	/** Below chest: also push hands forward of the hanging coat panels (hem). */
	float MinHemForwardCm = 14.f;
	float TorsoMinHeightCm = -48.f;
	float TorsoMaxHeightCm = 32.f;
	float MaxPushCm = 20.f;
	/** How quickly clearance IK eases on/off. Higher = snappier. */
	float InterpSpeed = 8.f;

	float SmoothedHandPushL = 0.f;
	float SmoothedHandPushR = 0.f;
	float SmoothedHandFwdPushL = 0.f;
	float SmoothedHandFwdPushR = 0.f;
	float SmoothedElbowPushL = 0.f;
	float SmoothedElbowPushR = 0.f;

private:
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;

	void SolveArm(
		FComponentSpacePoseContext& Output,
		FCompactPoseBoneIndex UpperIdx,
		FCompactPoseBoneIndex LowerIdx,
		FCompactPoseBoneIndex HandIdx,
		float SideSign,
		const FVector& Chest,
		const FVector& Forward,
		const FVector& Right,
		float HandPush,
		float HandFwdPush,
		float ElbowPush,
		TArray<FBoneTransform>& OutBoneTransforms) const;

	void ComputeArmPush(
		FComponentSpacePoseContext& Output,
		FCompactPoseBoneIndex LowerIdx,
		FCompactPoseBoneIndex HandIdx,
		float SideSign,
		const FVector& Chest,
		const FVector& Forward,
		const FVector& Right,
		float& OutHandPush,
		float& OutHandFwdPush,
		float& OutElbowPush) const;

	FBoneReference HandL;
	FBoneReference HandR;
	FBoneReference SpineChest;
	FBoneReference ClavicleL;
	FBoneReference ClavicleR;
	FBoneReference SpineUp;
	FBoneReference Pelvis;

	FCompactPoseBoneIndex UpperL = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex LowerL = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex UpperR = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex LowerR = FCompactPoseBoneIndex(INDEX_NONE);
};
