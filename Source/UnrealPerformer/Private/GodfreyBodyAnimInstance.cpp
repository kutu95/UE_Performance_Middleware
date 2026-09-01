#include "GodfreyBodyAnimInstance.h"

#include "Animation/AnimData/BoneMaskFilter.h"
#include "Animation/AnimSequence.h"
#include "GodfreyPerformanceLog.h"
#include "UnrealPerformerGodfreySettings.h"

const FName UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName(TEXT("DefaultSlot"));
const FName UGodfreyBodyAnimInstance::UpperBodyMontageSlotName(TEXT("UpperBody"));
const FName UGodfreyBodyAnimInstance::UpperBodyBlendBoneName(TEXT("spine_01"));
const FName UGodfreyBodyAnimInstance::NeckAimBoneName(TEXT("neck_01"));

namespace
{
const TCHAR* GGodfreyNeutralStancePaths[] = {
	TEXT("/Game/Godfrey/Animation/Animation/Performances/AS_IdleStanding_01_EyeFixed.AS_IdleStanding_01_EyeFixed"),
	TEXT("/Game/Godfrey/Animation/Animation/Performances/AS_IdleStanding_01.AS_IdleStanding_01"),
	TEXT("/Game/Godfrey/Animation/Animation/Performances/AS_IdleWeightShift_01_EyeFixed.AS_IdleWeightShift_01_EyeFixed"),
	TEXT("/Game/Godfrey/Animation/Animation/Performances/AS_IdleWeightShift_01.AS_IdleWeightShift_01"),
};
} // namespace

FGodfreyBodyAnimInstanceProxy::FGodfreyBodyAnimInstanceProxy(UAnimInstance* InAnimInstance)
	: FAnimInstanceProxy(InAnimInstance)
{
}

void FGodfreyBodyAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	ConstructNodes();
	if (UGodfreyBodyAnimInstance* const BodyInst = Cast<UGodfreyBodyAnimInstance>(InAnimInstance))
	{
		BodyInst->EnsureNeutralStanceSequenceLoaded();
		if (UAnimSequence* const Stance = BodyInst->GetNeutralStanceSequence())
		{
			NeutralStanceNode.SetSequence(Stance);
		}
	}
	FAnimInstanceProxy::Initialize(InAnimInstance);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyBodyAnimInstance: initialized (DefaultSlot=%s UpperBody=%s blendBone=%s neckLookAt=%s stance='%s')."),
		*UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName.ToString(),
		*UGodfreyBodyAnimInstance::UpperBodyMontageSlotName.ToString(),
		*UGodfreyBodyAnimInstance::UpperBodyBlendBoneName.ToString(),
		*UGodfreyBodyAnimInstance::NeckAimBoneName.ToString(),
		NeutralStanceNode.GetSequence() ? *NeutralStanceNode.GetSequence()->GetName() : TEXT("(none)"));
}

void FGodfreyBodyAnimInstanceProxy::Update(float DeltaSeconds)
{
	if (const UGodfreyBodyAnimInstance* const BodyInst = Cast<UGodfreyBodyAnimInstance>(GetAnimInstanceObject()))
	{
		if (LayeredBlendNode.BlendWeights.Num() > 0)
		{
			LayeredBlendNode.BlendWeights[0] = BodyInst->GetUpperBodyLayerWeight();
		}
		if (UAnimSequence* const Stance = BodyInst->GetNeutralStanceSequence())
		{
			if (NeutralStanceNode.GetSequence() != Stance)
			{
				NeutralStanceNode.SetSequence(Stance);
			}
		}
		NeckLookAtNode.LookAtLocation = BodyInst->GetHeadAimWorldTarget();
		NeckLookAtNode.LookAtClamp = BodyInst->GetHeadAimClampDegrees();
		NeckLookAtNode.Alpha = BodyInst->GetHeadAimAlpha();

		if (const UUnrealPerformerGodfreySettings* const Settings = GetDefault<UUnrealPerformerGodfreySettings>())
		{
			CoatClearanceNode.bEnabled = Settings->bGodfreyCoatClearance;
			CoatClearanceNode.Alpha = Settings->bGodfreyCoatClearance ? Settings->GodfreyCoatClearanceAlpha : 0.f;
			CoatClearanceNode.MinHandLateralCm = Settings->GodfreyCoatClearanceMinHandLateralCm;
			CoatClearanceNode.MinElbowLateralCm = Settings->GodfreyCoatClearanceMinElbowLateralCm;
			CoatClearanceNode.ForwardStartCm = Settings->GodfreyCoatClearanceForwardStartCm;
			CoatClearanceNode.MinChestForwardCm = Settings->GodfreyCoatClearanceMinChestForwardCm;
			CoatClearanceNode.MinHemForwardCm = Settings->GodfreyCoatClearanceMinHemForwardCm;
			CoatClearanceNode.HandRadiusCm = Settings->GodfreyCoatClearanceHandRadiusCm;
			CoatClearanceNode.BehindSkipCm = Settings->GodfreyCoatClearanceBehindSkipCm;
			CoatClearanceNode.TorsoMinHeightCm = Settings->GodfreyCoatClearanceTorsoMinHeightCm;
			CoatClearanceNode.TorsoMaxHeightCm = Settings->GodfreyCoatClearanceTorsoMaxHeightCm;
			CoatClearanceNode.MaxPushCm = Settings->GodfreyCoatClearanceMaxPushCm;
			CoatClearanceNode.InterpSpeed = Settings->GodfreyCoatClearanceInterpSpeed;
		}
	}

	FAnimInstanceProxy::Update(DeltaSeconds);
}

FAnimNode_Base* FGodfreyBodyAnimInstanceProxy::GetCustomRootNode()
{
	return &RootNode;
}

void FGodfreyBodyAnimInstanceProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Add(&NeutralStanceNode);
	OutNodes.Add(&DefaultSlotNode);
	OutNodes.Add(&UpperBodySlotNode);
	OutNodes.Add(&LayeredBlendNode);
	OutNodes.Add(&LocalToCSNode);
	OutNodes.Add(&NeckLookAtNode);
	OutNodes.Add(&CoatClearanceNode);
	OutNodes.Add(&CSToLocalNode);
	OutNodes.Add(&RootNode);
}

void FGodfreyBodyAnimInstanceProxy::ConstructNodes()
{
	NeutralStanceNode.SetLoopAnimation(true);
	NeutralStanceNode.SetPlayRate(0.72f);

	DefaultSlotNode.SlotName = UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName;
	DefaultSlotNode.Source.SetLinkNode(&NeutralStanceNode);

	UpperBodySlotNode.SlotName = UGodfreyBodyAnimInstance::UpperBodyMontageSlotName;
	UpperBodySlotNode.Source.SetLinkNode(&DefaultSlotNode);

	LayeredBlendNode.BlendMode = ELayeredBoneBlendMode::BranchFilter;
	LayeredBlendNode.bMeshSpaceRotationBlend = true;
	LayeredBlendNode.bBlendRootMotionBasedOnRootBone = true;
	if (LayeredBlendNode.BlendPoses.Num() == 0)
	{
		LayeredBlendNode.AddPose();
	}
	LayeredBlendNode.BasePose.SetLinkNode(&DefaultSlotNode);
	LayeredBlendNode.BlendPoses[0].SetLinkNode(&UpperBodySlotNode);
	LayeredBlendNode.BlendWeights[0] = 0.f;

	if (LayeredBlendNode.LayerSetup.Num() > 0)
	{
		FBranchFilter Filter;
		Filter.BoneName = UGodfreyBodyAnimInstance::UpperBodyBlendBoneName;
		Filter.BlendDepth = 0;
		LayeredBlendNode.LayerSetup[0].BranchFilters.Reset();
		LayeredBlendNode.LayerSetup[0].BranchFilters.Add(Filter);
	}
	LayeredBlendNode.InvalidatePerBoneBlendWeights();

	LocalToCSNode.LocalPose.SetLinkNode(&LayeredBlendNode);

	NeckLookAtNode.ComponentPose.SetLinkNode(&LocalToCSNode);
	NeckLookAtNode.BoneToModify.BoneName = UGodfreyBodyAnimInstance::NeckAimBoneName;
	NeckLookAtNode.LookAtClamp = 16.f;
	NeckLookAtNode.InterpolationTime = 0.18f;
	NeckLookAtNode.Alpha = 0.f;
	NeckLookAtNode.bAlphaBoolEnabled = true;

	CoatClearanceNode.ComponentPose.SetLinkNode(&NeckLookAtNode);
	CoatClearanceNode.Alpha = 1.f;
	CoatClearanceNode.bAlphaBoolEnabled = true;

	CSToLocalNode.ComponentPose.SetLinkNode(&CoatClearanceNode);
	RootNode.Result.SetLinkNode(&CSToLocalNode);
}

UGodfreyBodyAnimInstance::UGodfreyBodyAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FAnimInstanceProxy* UGodfreyBodyAnimInstance::CreateAnimInstanceProxy()
{
	return new FGodfreyBodyAnimInstanceProxy(this);
}

void UGodfreyBodyAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}

void UGodfreyBodyAnimInstance::EnsureNeutralStanceSequenceLoaded()
{
	if (IsValid(NeutralStanceSequence))
	{
		return;
	}
	const bool bPreferEyeFixed = GetDefault<UUnrealPerformerGodfreySettings>()
		&& GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyPreferEyeFixedLibraryVariants;
	for (const TCHAR* Path : GGodfreyNeutralStancePaths)
	{
		if (!bPreferEyeFixed && FString(Path).Contains(TEXT("_EyeFixed"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (UAnimSequence* const Seq = LoadObject<UAnimSequence>(nullptr, Path))
		{
			NeutralStanceSequence = Seq;
			return;
		}
	}
}

void UGodfreyBodyAnimInstance::SetNeutralStanceSequence(UAnimSequence* Sequence)
{
	if (IsValid(Sequence))
	{
		NeutralStanceSequence = Sequence;
	}
}

void UGodfreyBodyAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	EnsureNeutralStanceSequenceLoaded();
}

void UGodfreyBodyAnimInstance::SetUpperBodyLayerWeight(const float NewWeight)
{
	UpperBodyLayerWeight = FMath::Clamp(NewWeight, 0.f, 1.f);
}

void UGodfreyBodyAnimInstance::SetConversationHeadAim(const bool bEnable, const FVector& WorldTarget,
	const float ClampDegrees, const float Alpha)
{
	bHeadAimEnabled = bEnable;
	HeadAimWorldTarget = WorldTarget;
	HeadAimClampDegrees = FMath::Clamp(ClampDegrees, 1.f, 45.f);
	HeadAimAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
}

void UGodfreyBodyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!IsValid(NeutralStanceSequence))
	{
		EnsureNeutralStanceSequenceLoaded();
	}
	if (IsValid(NeutralStanceSequence) && !bLoggedNeutralStance)
	{
		bLoggedNeutralStance = true;
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyBodyAnimInstance: slot source is looping '%s' (not RefPose)."),
			*NeutralStanceSequence->GetName());
	}

	if (bHeadAimEnabled && HeadAimAlpha > KINDA_SMALL_NUMBER)
	{
		if (!bLoggedHeadAim)
		{
			bLoggedHeadAim = true;
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyBodyAnimInstance: neck LookAt on — target=(%.1f,%.1f,%.1f) clamp=%.1f alpha=%.2f."),
				HeadAimWorldTarget.X, HeadAimWorldTarget.Y, HeadAimWorldTarget.Z,
				HeadAimClampDegrees, HeadAimAlpha);
		}
	}
	else
	{
		bLoggedHeadAim = false;
	}

	if (bLoggedActiveSlotWeight)
	{
		return;
	}

	const float DefaultWeight = GetSlotMontageGlobalWeight(DefaultBodyMontageSlotName);
	const float UpperWeight = GetSlotMontageGlobalWeight(UpperBodyMontageSlotName);
	const bool bMontageActive = IsAnyMontagePlaying();
	if (DefaultWeight <= KINDA_SMALL_NUMBER && UpperWeight <= KINDA_SMALL_NUMBER && !bMontageActive)
	{
		return;
	}

	bLoggedActiveSlotWeight = true;
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyBodyAnimInstance: DefaultSlot=%.2f UpperBody=%.2f layer=%.2f montageActive=%d."),
		DefaultWeight,
		UpperWeight,
		UpperBodyLayerWeight,
		bMontageActive ? 1 : 0);
}
