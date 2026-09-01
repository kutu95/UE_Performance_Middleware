#include "GodfreyCoatClearanceAnimNode.h"

#include "Animation/AnimInstanceProxy.h"
#include "GodfreyPerformanceLog.h"
#include "TwoBoneIK.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GodfreyCoatClearanceAnimNode)

FAnimNode_GodfreyCoatClearance::FAnimNode_GodfreyCoatClearance()
{
	Alpha = 1.f;
	bAlphaBoolEnabled = true;
	LODThreshold = INDEX_NONE;
	HandL.BoneName = FName(TEXT("hand_l"));
	HandR.BoneName = FName(TEXT("hand_r"));
	SpineChest.BoneName = FName(TEXT("spine_03"));
	ClavicleL.BoneName = FName(TEXT("clavicle_l"));
	ClavicleR.BoneName = FName(TEXT("clavicle_r"));
	SpineUp.BoneName = FName(TEXT("spine_05"));
	Pelvis.BoneName = FName(TEXT("pelvis"));
}

void FAnimNode_GodfreyCoatClearance::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	HandL.Initialize(RequiredBones);
	HandR.Initialize(RequiredBones);
	SpineChest.Initialize(RequiredBones);
	ClavicleL.Initialize(RequiredBones);
	ClavicleR.Initialize(RequiredBones);
	SpineUp.Initialize(RequiredBones);
	Pelvis.Initialize(RequiredBones);

	UpperL = FCompactPoseBoneIndex(INDEX_NONE);
	LowerL = FCompactPoseBoneIndex(INDEX_NONE);
	UpperR = FCompactPoseBoneIndex(INDEX_NONE);
	LowerR = FCompactPoseBoneIndex(INDEX_NONE);

	const FCompactPoseBoneIndex HandLIdx = HandL.GetCompactPoseIndex(RequiredBones);
	if (HandLIdx != INDEX_NONE)
	{
		LowerL = RequiredBones.GetParentBoneIndex(HandLIdx);
		if (LowerL != INDEX_NONE)
		{
			UpperL = RequiredBones.GetParentBoneIndex(LowerL);
		}
	}

	const FCompactPoseBoneIndex HandRIdx = HandR.GetCompactPoseIndex(RequiredBones);
	if (HandRIdx != INDEX_NONE)
	{
		LowerR = RequiredBones.GetParentBoneIndex(HandRIdx);
		if (LowerR != INDEX_NONE)
		{
			UpperR = RequiredBones.GetParentBoneIndex(LowerR);
		}
	}
}

bool FAnimNode_GodfreyCoatClearance::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	(void)Skeleton;
	return bEnabled
		&& HandL.IsValidToEvaluate(RequiredBones)
		&& HandR.IsValidToEvaluate(RequiredBones)
		&& SpineChest.IsValidToEvaluate(RequiredBones)
		&& ClavicleL.IsValidToEvaluate(RequiredBones)
		&& ClavicleR.IsValidToEvaluate(RequiredBones)
		&& UpperL != INDEX_NONE && LowerL != INDEX_NONE
		&& UpperR != INDEX_NONE && LowerR != INDEX_NONE;
}

void FAnimNode_GodfreyCoatClearance::ComputeArmPush(
	FComponentSpacePoseContext& Output,
	const FCompactPoseBoneIndex LowerIdx,
	const FCompactPoseBoneIndex HandIdx,
	const float SideSign,
	const FVector& Chest,
	const FVector& Forward,
	const FVector& Right,
	float& OutHandPush,
	float& OutHandFwdPush,
	float& OutElbowPush) const
{
	OutHandPush = 0.f;
	OutHandFwdPush = 0.f;
	OutElbowPush = 0.f;
	if (LowerIdx == INDEX_NONE || HandIdx == INDEX_NONE)
	{
		return;
	}

	const FTransform HandCS = Output.Pose.GetComponentSpaceTransform(HandIdx);
	const FVector WristLoc = HandCS.GetLocation();
	const FVector ElbowLoc = Output.Pose.GetComponentSpaceTransform(LowerIdx).GetLocation();
	// MetaHuman hand_l/r X runs wrist → knuckles. Fingers clip the coat before the wrist bone does.
	const FVector PalmLoc = WristLoc + HandCS.GetUnitAxis(EAxis::X) * FMath::Max(0.f, HandRadiusCm);

	auto AccumulateHandSample = [this, SideSign, &Chest, &Forward, &Right, &OutHandPush, &OutHandFwdPush](const FVector& SampleLoc)
	{
		const FVector Rel = SampleLoc - Chest;
		const float Fwd = FVector::DotProduct(Rel, Forward);
		const float Lat = FVector::DotProduct(Rel, Right);
		const float Hgt = Rel.Z;
		if (Hgt < TorsoMinHeightCm || Hgt > TorsoMaxHeightCm)
		{
			return;
		}
		// Behind the back (HandsBehindBack) — leave alone.
		if (Fwd < BehindSkipCm)
		{
			return;
		}

		const float DesiredLat = SideSign * MinHandLateralCm;
		const float NewLat = (SideSign < 0.f)
			? FMath::Min(Lat, DesiredLat)
			: FMath::Max(Lat, DesiredLat);
		const float ThisPush = FMath::Clamp(NewLat - Lat, -MaxPushCm, MaxPushCm);
		if (FMath::Abs(ThisPush) > FMath::Abs(OutHandPush))
		{
			OutHandPush = ThisPush;
		}

		// Only push forward while the sample still overlaps the coat body in Y.
		// Hanging-at-sides hands are already past MinHandLateral and stay put.
		if (FMath::Abs(Lat) < MinHandLateralCm)
		{
			const float MinFwd = (Hgt < 0.f) ? MinHemForwardCm : MinChestForwardCm;
			OutHandFwdPush = FMath::Max(OutHandFwdPush, FMath::Clamp(MinFwd - Fwd, 0.f, MaxPushCm));
		}
	};

	AccumulateHandSample(WristLoc);
	AccumulateHandSample(PalmLoc);

	const FVector RelElbow = ElbowLoc - Chest;
	const float ElbowFwd = FVector::DotProduct(RelElbow, Forward);
	const float ElbowLat = FVector::DotProduct(RelElbow, Right);
	const float ElbowHgt = RelElbow.Z;
	const bool bElbowInCoatBand = ElbowHgt >= TorsoMinHeightCm && ElbowHgt <= TorsoMaxHeightCm;
	if (bElbowInCoatBand && ElbowFwd >= BehindSkipCm)
	{
		const float DesiredElbowLat = SideSign * MinElbowLateralCm;
		const float NewElbowLat = (SideSign < 0.f)
			? FMath::Min(ElbowLat, DesiredElbowLat)
			: FMath::Max(ElbowLat, DesiredElbowLat);
		OutElbowPush = FMath::Clamp(NewElbowLat - ElbowLat, -MaxPushCm, MaxPushCm);
	}
}

void FAnimNode_GodfreyCoatClearance::SolveArm(
	FComponentSpacePoseContext& Output,
	const FCompactPoseBoneIndex UpperIdx,
	const FCompactPoseBoneIndex LowerIdx,
	const FCompactPoseBoneIndex HandIdx,
	const float SideSign,
	const FVector& Chest,
	const FVector& Forward,
	const FVector& Right,
	const float HandPush,
	const float HandFwdPush,
	const float ElbowPush,
	TArray<FBoneTransform>& OutBoneTransforms) const
{
	(void)Chest;
	(void)SideSign;
	if (UpperIdx == INDEX_NONE || LowerIdx == INDEX_NONE || HandIdx == INDEX_NONE)
	{
		return;
	}
	if (FMath::Abs(HandPush) < 0.05f && FMath::Abs(HandFwdPush) < 0.05f && FMath::Abs(ElbowPush) < 0.05f)
	{
		return;
	}

	FTransform UpperCS = Output.Pose.GetComponentSpaceTransform(UpperIdx);
	FTransform LowerCS = Output.Pose.GetComponentSpaceTransform(LowerIdx);
	FTransform HandCS = Output.Pose.GetComponentSpaceTransform(HandIdx);
	const FVector DesiredHand = HandCS.GetLocation() + Right * HandPush + Forward * HandFwdPush;
	const FVector JointTarget = LowerCS.GetLocation() + Right * ElbowPush + Forward * (HandFwdPush * 0.5f);

	AnimationCore::SolveTwoBoneIK(UpperCS, LowerCS, HandCS, JointTarget, DesiredHand, false, 1.0, 1.0);
	OutBoneTransforms.Add(FBoneTransform(UpperIdx, UpperCS));
	OutBoneTransforms.Add(FBoneTransform(LowerIdx, LowerCS));
	OutBoneTransforms.Add(FBoneTransform(HandIdx, HandCS));
}

void FAnimNode_GodfreyCoatClearance::EvaluateSkeletalControl_AnyThread(
	FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	check(OutBoneTransforms.Num() == 0);

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex ChestIdx = SpineChest.GetCompactPoseIndex(BoneContainer);
	const FCompactPoseBoneIndex ClavLIdx = ClavicleL.GetCompactPoseIndex(BoneContainer);
	const FCompactPoseBoneIndex ClavRIdx = ClavicleR.GetCompactPoseIndex(BoneContainer);
	if (ChestIdx == INDEX_NONE || ClavLIdx == INDEX_NONE || ClavRIdx == INDEX_NONE)
	{
		return;
	}

	const FVector Chest = Output.Pose.GetComponentSpaceTransform(ChestIdx).GetLocation();
	const FVector ClavL = Output.Pose.GetComponentSpaceTransform(ClavLIdx).GetLocation();
	const FVector ClavR = Output.Pose.GetComponentSpaceTransform(ClavRIdx).GetLocation();
	FVector Right = (ClavR - ClavL).GetSafeNormal();
	FVector Up = FVector::UpVector;
	if (SpineUp.IsValidToEvaluate(BoneContainer) && Pelvis.IsValidToEvaluate(BoneContainer))
	{
		const FCompactPoseBoneIndex UpIdx = SpineUp.GetCompactPoseIndex(BoneContainer);
		const FCompactPoseBoneIndex PelvisIdx = Pelvis.GetCompactPoseIndex(BoneContainer);
		if (UpIdx != INDEX_NONE && PelvisIdx != INDEX_NONE)
		{
			Up = (Output.Pose.GetComponentSpaceTransform(UpIdx).GetLocation()
				- Output.Pose.GetComponentSpaceTransform(PelvisIdx).GetLocation()).GetSafeNormal();
		}
	}
	FVector Forward = FVector::CrossProduct(Right, Up).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
		Right = FVector::RightVector;
	}

	float DesiredHandL = 0.f;
	float DesiredHandFwdL = 0.f;
	float DesiredElbowL = 0.f;
	float DesiredHandR = 0.f;
	float DesiredHandFwdR = 0.f;
	float DesiredElbowR = 0.f;
	ComputeArmPush(Output, LowerL, HandL.GetCompactPoseIndex(BoneContainer),
		-1.f, Chest, Forward, Right, DesiredHandL, DesiredHandFwdL, DesiredElbowL);
	ComputeArmPush(Output, LowerR, HandR.GetCompactPoseIndex(BoneContainer),
		1.f, Chest, Forward, Right, DesiredHandR, DesiredHandFwdR, DesiredElbowR);

	const float Dt = Output.AnimInstanceProxy ? Output.AnimInstanceProxy->GetDeltaSeconds() : 0.016f;
	const float Speed = FMath::Max(0.1f, InterpSpeed);
	SmoothedHandPushL = FMath::FInterpTo(SmoothedHandPushL, DesiredHandL, Dt, Speed);
	SmoothedHandFwdPushL = FMath::FInterpTo(SmoothedHandFwdPushL, DesiredHandFwdL, Dt, Speed);
	SmoothedElbowPushL = FMath::FInterpTo(SmoothedElbowPushL, DesiredElbowL, Dt, Speed);
	SmoothedHandPushR = FMath::FInterpTo(SmoothedHandPushR, DesiredHandR, Dt, Speed);
	SmoothedHandFwdPushR = FMath::FInterpTo(SmoothedHandFwdPushR, DesiredHandFwdR, Dt, Speed);
	SmoothedElbowPushR = FMath::FInterpTo(SmoothedElbowPushR, DesiredElbowR, Dt, Speed);

	const int32 NumBefore = OutBoneTransforms.Num();
	SolveArm(Output, UpperL, LowerL, HandL.GetCompactPoseIndex(BoneContainer),
		-1.f, Chest, Forward, Right, SmoothedHandPushL, SmoothedHandFwdPushL, SmoothedElbowPushL, OutBoneTransforms);
	SolveArm(Output, UpperR, LowerR, HandR.GetCompactPoseIndex(BoneContainer),
		1.f, Chest, Forward, Right, SmoothedHandPushR, SmoothedHandFwdPushR, SmoothedElbowPushR, OutBoneTransforms);

	if (OutBoneTransforms.Num() > NumBefore)
	{
		OutBoneTransforms.Sort(FCompareBoneTransformIndex());
		static bool bLoggedPush = false;
		if (!bLoggedPush)
		{
			bLoggedPush = true;
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyCoatClearance: pushing arms off jacket (bones=%d)."),
				OutBoneTransforms.Num());
		}
	}
}
