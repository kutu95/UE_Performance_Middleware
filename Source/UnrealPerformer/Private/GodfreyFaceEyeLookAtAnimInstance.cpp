#include "GodfreyFaceEyeLookAtAnimInstance.h"

#include "GodfreyPerformanceLog.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/KismetMathLibrary.h"

namespace
{
static bool ComputeEyeLookCurveValues(
	const USkeletalMeshComponent* FaceMesh,
	const FName HeadBoneName,
	const FVector& TargetWorldLocation,
	const float MaxYawDegrees,
	const float MaxPitchDegrees,
	const float Alpha,
	TMap<FName, float>& OutCurveMap)
{
	if (!IsValid(FaceMesh) || HeadBoneName.IsNone() || Alpha <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const int32 HeadBoneIndex = FaceMesh->GetBoneIndex(HeadBoneName);
	if (HeadBoneIndex == INDEX_NONE)
	{
		return false;
	}

	const FTransform HeadWorld = FaceMesh->GetBoneTransform(HeadBoneIndex);
	const FRotator HeadRotation = HeadWorld.Rotator();
	const FRotator LookRotation =
		UKismetMathLibrary::FindLookAtRotation(HeadWorld.GetLocation(), TargetWorldLocation);

	float YawOffset = FRotator::NormalizeAxis(LookRotation.Yaw - HeadRotation.Yaw);
	float PitchOffset = FRotator::NormalizeAxis(LookRotation.Pitch - HeadRotation.Pitch);

	const float SafeMaxYaw = FMath::Max(5.f, MaxYawDegrees);
	const float SafeMaxPitch = FMath::Max(5.f, MaxPitchDegrees);
	YawOffset = FMath::Clamp(YawOffset, -SafeMaxYaw, SafeMaxYaw);
	PitchOffset = FMath::Clamp(PitchOffset, -SafeMaxPitch, SafeMaxPitch);

	const float Blend = FMath::Clamp(Alpha, 0.f, 1.f);
	const float Up = FMath::Clamp(PitchOffset / SafeMaxPitch, 0.f, 1.f) * Blend;
	const float Down = FMath::Clamp(-PitchOffset / SafeMaxPitch, 0.f, 1.f) * Blend;
	const float Right = FMath::Clamp(YawOffset / SafeMaxYaw, 0.f, 1.f) * Blend;
	const float Left = FMath::Clamp(-YawOffset / SafeMaxYaw, 0.f, 1.f) * Blend;

	OutCurveMap.Add(FName(TEXT("CTRL_expressions_eyeLookUpL")), Up);
	OutCurveMap.Add(FName(TEXT("CTRL_expressions_eyeLookUpR")), Up);
	OutCurveMap.Add(FName(TEXT("CTRL_expressions_eyeLookDownL")), Down);
	OutCurveMap.Add(FName(TEXT("CTRL_expressions_eyeLookDownR")), Down);
	OutCurveMap.Add(FName(TEXT("CTRL_expressions_eyeLookLeftL")), Left);
	OutCurveMap.Add(FName(TEXT("CTRL_expressions_eyeLookLeftR")), Left);
	OutCurveMap.Add(FName(TEXT("CTRL_expressions_eyeLookRightL")), Right);
	OutCurveMap.Add(FName(TEXT("CTRL_expressions_eyeLookRightR")), Right);
	OutCurveMap.Add(FName(TEXT("CTRL_expressions_eyeParallelLookDirection")), Blend);
	return true;
}
} // namespace

FGodfreyFaceEyeLookAtAnimInstanceProxy::FGodfreyFaceEyeLookAtAnimInstanceProxy(UAnimInstance* InAnimInstance)
	: FAnimInstanceProxy(InAnimInstance)
{
}

void FGodfreyFaceEyeLookAtAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);
	ConstructNodes();
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyFaceEyeLookAt: post-process initialized (ModifyCurve eyeLook only — no bone LookAt)."));
}

void FGodfreyFaceEyeLookAtAnimInstanceProxy::UpdateEyeLookCurves(
	const UGodfreyFaceEyeLookAtAnimInstance* FaceInst)
{
	ModifyCurveNode.CurveMap.Reset();
	if (!FaceInst || !FaceInst->bEnableEyeLookAt)
	{
		ModifyCurveNode.Alpha = 0.f;
		return;
	}

	USkeletalMeshComponent* const Mesh = FaceInst->GetOwningComponent();
	if (!IsValid(Mesh))
	{
		ModifyCurveNode.Alpha = 0.f;
		return;
	}

	TMap<FName, float> Curves;
	if (ComputeEyeLookCurveValues(
			Mesh,
			FaceInst->HeadBoneNameForCurveAim,
			FaceInst->EyeLookAtWorldLocation,
			FaceInst->EyeLookAtMaxYawDegrees,
			FaceInst->EyeLookAtMaxPitchDegrees,
			FaceInst->EyeLookAtAlpha,
			Curves))
	{
		ModifyCurveNode.CurveMap = MoveTemp(Curves);
		ModifyCurveNode.ApplyMode = EModifyCurveApplyMode::Blend;
		ModifyCurveNode.Alpha = FMath::Clamp(FaceInst->EyeLookAtAlpha, 0.f, 1.f);
	}
	else
	{
		ModifyCurveNode.Alpha = 0.f;
	}
}

void FGodfreyFaceEyeLookAtAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	const UGodfreyFaceEyeLookAtAnimInstance* const FaceInst =
		Cast<UGodfreyFaceEyeLookAtAnimInstance>(InAnimInstance);
	UpdateEyeLookCurves(FaceInst);
}

FAnimNode_Base* FGodfreyFaceEyeLookAtAnimInstanceProxy::GetCustomRootNode()
{
	return &RootNode;
}

void FGodfreyFaceEyeLookAtAnimInstanceProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Add(&InputPoseNode);
	OutNodes.Add(&ModifyCurveNode);
	OutNodes.Add(&RootNode);
}

void FGodfreyFaceEyeLookAtAnimInstanceProxy::ConstructNodes()
{
	InputPoseNode.Name = FAnimNode_LinkedInputPose::DefaultInputPoseName;

	ModifyCurveNode.ApplyMode = EModifyCurveApplyMode::Blend;
	ModifyCurveNode.Alpha = 0.f;
	ModifyCurveNode.SourcePose.SetLinkNode(&InputPoseNode);

	RootNode.Result.SetLinkNode(&ModifyCurveNode);
}

UGodfreyFaceEyeLookAtAnimInstance::UGodfreyFaceEyeLookAtAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FAnimInstanceProxy* UGodfreyFaceEyeLookAtAnimInstance::CreateAnimInstanceProxy()
{
	return new FGodfreyFaceEyeLookAtAnimInstanceProxy(this);
}

void UGodfreyFaceEyeLookAtAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}

void UGodfreyFaceEyeLookAtAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (bEnableEyeLookAt && !bLoggedEnable)
	{
		bLoggedEnable = true;
		USkeletalMeshComponent* const Mesh = GetOwningComponent();
		const bool bHasHead =
			IsValid(Mesh) && Mesh->GetBoneIndex(HeadBoneNameForCurveAim) != INDEX_NONE;
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyFaceEyeLookAt: enabled -> cam=(%.1f,%.1f,%.1f) alpha=%.2f headBone=%s (%s)."),
			EyeLookAtWorldLocation.X,
			EyeLookAtWorldLocation.Y,
			EyeLookAtWorldLocation.Z,
			EyeLookAtAlpha,
			*HeadBoneNameForCurveAim.ToString(),
			bHasHead ? TEXT("ok") : TEXT("missing"));
	}
	else if (!bEnableEyeLookAt)
	{
		bLoggedEnable = false;
	}
}
