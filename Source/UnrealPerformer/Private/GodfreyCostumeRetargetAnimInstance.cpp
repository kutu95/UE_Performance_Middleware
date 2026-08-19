#include "GodfreyCostumeRetargetAnimInstance.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "GodfreyPerformanceLog.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetProcessor.h"

FGodfreyCostumeRetargetAnimInstanceProxy::FGodfreyCostumeRetargetAnimInstanceProxy(
	UAnimInstance* InAnimInstance,
	FAnimNode_Root* InRootNode,
	FAnimNode_RetargetPoseFromMesh* InRetargetNode)
	: FAnimInstanceProxy(InAnimInstance)
	, RootNode(InRootNode)
	, RetargetNode(InRetargetNode)
{
}

void FGodfreyCostumeRetargetAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	if (RootNode && RetargetNode)
	{
		RetargetNode->RetargetFrom = ERetargetSourceMode::CustomSkeletalMeshComponent;
		RetargetNode->bSuppressWarnings = false;
		RetargetNode->LODThreshold = -1;
		RetargetNode->LODThresholdForIK = -1;
		RootNode->Result.SetLinkNode(RetargetNode);
	}

	FAnimInstanceProxy::Initialize(InAnimInstance);

	if (RootNode && RetargetNode)
	{
		FAnimationInitializeContext InitContext(this);
		RetargetNode->Initialize_AnyThread(InitContext);
		RootNode->Initialize_AnyThread(InitContext);
	}
}

bool FGodfreyCostumeRetargetAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	if (RetargetNode)
	{
		RetargetNode->Evaluate_AnyThread(Output);
		return true;
	}
	return FAnimInstanceProxy::Evaluate(Output);
}

FAnimNode_Base* FGodfreyCostumeRetargetAnimInstanceProxy::GetCustomRootNode()
{
	return RootNode;
}

void FGodfreyCostumeRetargetAnimInstanceProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	if (RetargetNode)
	{
		OutNodes.Add(RetargetNode);
	}
	if (RootNode)
	{
		OutNodes.Add(RootNode);
	}
}

bool FGodfreyCostumeRetargetAnimInstanceProxy::ConfigureRetarget(
	UIKRetargeter* InRetargeter,
	USkeletalMeshComponent* InSourceMesh)
{
	if (!RetargetNode)
	{
		return false;
	}

	const bool bChanged =
		RetargetNode->IKRetargeterAsset != InRetargeter
		|| RetargetNode->SourceMeshComponent.Get() != InSourceMesh
		|| RetargetNode->RetargetFrom != ERetargetSourceMode::CustomSkeletalMeshComponent;

	RetargetNode->IKRetargeterAsset = InRetargeter;
	RetargetNode->RetargetFrom = ERetargetSourceMode::CustomSkeletalMeshComponent;
	RetargetNode->SourceMeshComponent = InSourceMesh;

	// Only bump shared RTG version when config actually changes. Calling this every
	// frame clears Processor.IsInitialized before PreUpdate and leaves the costume
	// permanently in ref/T-pose (Evaluate sees an empty PoseToRetarget buffer).
	if (bChanged)
	{
		if (FIKRetargetProcessor* const Processor = RetargetNode->GetRetargetProcessor())
		{
			Processor->SetNeedsInitialized();
		}
	}

	return bChanged;
}

UGodfreyCostumeRetargetAnimInstance::UGodfreyCostumeRetargetAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseMultiThreadedAnimationUpdate = false;
}

FAnimInstanceProxy* UGodfreyCostumeRetargetAnimInstance::CreateAnimInstanceProxy()
{
	return new FGodfreyCostumeRetargetAnimInstanceProxy(this, &RootNode, &RetargetNode);
}

void UGodfreyCostumeRetargetAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}

void UGodfreyCostumeRetargetAnimInstance::ResolveSourceAndRetargeter()
{
	if (!IKRetargeterAsset && DefaultIKRetargeterPath.IsValid())
	{
		IKRetargeterAsset = Cast<UIKRetargeter>(DefaultIKRetargeterPath.TryLoad());
	}

	if (!SourceMeshComponent)
	{
		if (AActor* const Owner = GetOwningActor())
		{
			TArray<USkeletalMeshComponent*> Meshes;
			Owner->GetComponents<USkeletalMeshComponent>(Meshes);
			for (USkeletalMeshComponent* Mesh : Meshes)
			{
				if (Mesh && Mesh->GetFName() == SourceMeshComponentName)
				{
					SourceMeshComponent = Mesh;
					break;
				}
			}
			if (!SourceMeshComponent)
			{
				for (USkeletalMeshComponent* Mesh : Meshes)
				{
					if (Mesh && Mesh->GetName().Contains(TEXT("Body")))
					{
						SourceMeshComponent = Mesh;
						break;
					}
				}
			}
		}
	}
}

void UGodfreyCostumeRetargetAnimInstance::EnsureCostumeTicksAfterSource()
{
	USkeletalMeshComponent* const CostumeMesh = GetSkelMeshComponent();
	USkeletalMeshComponent* const Body = SourceMeshComponent.Get();
	if (!CostumeMesh || !Body || CostumeMesh == Body)
	{
		return;
	}
	CostumeMesh->AddTickPrerequisiteComponent(Body);
}

bool UGodfreyCostumeRetargetAnimInstance::PushRetargetConfigToProxy()
{
	FGodfreyCostumeRetargetAnimInstanceProxy& Proxy =
		GetProxyOnGameThread<FGodfreyCostumeRetargetAnimInstanceProxy>();
	return Proxy.ConfigureRetarget(IKRetargeterAsset, SourceMeshComponent);
}

void UGodfreyCostumeRetargetAnimInstance::ForceProcessorReady()
{
	if (USkeletalMeshComponent* const CostumeMesh = GetSkelMeshComponent())
	{
		if (IKRetargeterAsset && SourceMeshComponent)
		{
			// Must run before PreUpdate so the source CS pose is copied that frame.
			RetargetNode.EnsureProcessorIsInitialized(CostumeMesh);
		}
	}
}

void UGodfreyCostumeRetargetAnimInstance::ApplyCostumePresentationFixes()
{
	USkeletalMeshComponent* const CostumeMesh = GetSkelMeshComponent();
	USkeletalMeshComponent* const Body = SourceMeshComponent.Get();
	if (!CostumeMesh)
	{
		return;
	}

	const bool bBootsOnly = CostumeMesh->ComponentHasTag(FName(TEXT("VictorianBootsOnly")));

	static const FName CostumeBonesToShow[] = {
		FName(TEXT("hat")), FName(TEXT("Hat")),
		FName(TEXT("hatband")), FName(TEXT("hatinside")), FName(TEXT("Hatband")),
		FName(TEXT("head")), FName(TEXT("Head")),
	};
	if (!bBootsOnly)
	{
		for (const FName BoneName : CostumeBonesToShow)
		{
			if (CostumeMesh->GetBoneIndex(BoneName) != INDEX_NONE)
			{
				CostumeMesh->UnHideBoneByName(BoneName);
			}
		}
	}

	if (!bCostumeMaterialsRestored)
	{
		if (USkeletalMesh* const Skel = CostumeMesh->GetSkeletalMeshAsset())
		{
			CostumeMesh->EmptyOverrideMaterials();
			const TArray<FSkeletalMaterial>& Mats = Skel->GetMaterials();
			for (int32 Index = 0; Index < Mats.Num(); ++Index)
			{
				const FString Slot = Mats[Index].MaterialSlotName.ToString();
				const bool bGenesisFaceOnly =
					Slot.Contains(TEXT("eyelash"), ESearchCase::IgnoreCase)
					|| Slot.Contains(TEXT("EyeMoisture"), ESearchCase::IgnoreCase)
					|| Slot.Equals(TEXT("Material"), ESearchCase::IgnoreCase);
				const bool bVictorianShoe =
					Slot.Contains(TEXT("sole"), ESearchCase::IgnoreCase)
					|| Slot.Contains(TEXT("toe"), ESearchCase::IgnoreCase)
					|| Slot.Contains(TEXT("shank"), ESearchCase::IgnoreCase)
					|| Slot.Equals(TEXT("upper"), ESearchCase::IgnoreCase)
					|| Slot.StartsWith(TEXT("upper_"), ESearchCase::IgnoreCase);
				if (bGenesisFaceOnly || (bBootsOnly && !bVictorianShoe))
				{
					CostumeMesh->SetMaterial(Index, nullptr);
				}
			}
			bCostumeMaterialsRestored = true;
		}
	}

	if (!Body)
	{
		return;
	}

	// Retarget assumes costume sits at Body origin. Fix location/rotation once.
	// Do NOT force scale to 1 — Genesis proportions need a calibrated RelativeScale3D.
	// Boots-only keeps Python-calibrated relative location (foot alignment).
	if (!bCostumeTransformFixed)
	{
		if (CostumeMesh->GetAttachParent() != Body)
		{
			CostumeMesh->AttachToComponent(
				Body, FAttachmentTransformRules::KeepRelativeTransform);
		}
		if (!bBootsOnly)
		{
			CostumeMesh->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		}
		bCostumeTransformFixed = true;
	}

	Body->SetHiddenInGame(false, true);
	Body->SetVisibility(true, true);
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	static const FName BodyArmRoots[] = {
		FName(TEXT("clavicle_l")), FName(TEXT("clavicle_r")),
		FName(TEXT("upperarm_l")), FName(TEXT("upperarm_r")),
		FName(TEXT("lowerarm_l")), FName(TEXT("lowerarm_r")),
		FName(TEXT("hand_l")), FName(TEXT("hand_r")),
	};
	for (const FName BoneName : BodyArmRoots)
	{
		if (Body->GetBoneIndex(BoneName) != INDEX_NONE)
		{
			Body->UnHideBoneByName(BoneName);
		}
	}

	// Do not HideBone thighs while alignment is unstable — that looks like a torn body
	// (floating torso + shoes at the coat hem). Re-enable after the suit sits correctly.
	static const FName BodyLegRoots[] = {
		FName(TEXT("thigh_l")),
		FName(TEXT("thigh_r")),
	};
	for (const FName BoneName : BodyLegRoots)
	{
		if (Body->GetBoneIndex(BoneName) != INDEX_NONE)
		{
			Body->UnHideBoneByName(BoneName);
		}
	}
}

void UGodfreyCostumeRetargetAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	ResolveSourceAndRetargeter();
	EnsureCostumeTicksAfterSource();
	PushRetargetConfigToProxy();
	ForceProcessorReady();

	if (SourceMeshComponent)
	{
		SourceMeshComponent->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		SourceMeshComponent->bUpdateOverlapsOnAnimationFinalize = false;
	}

	if (USkeletalMeshComponent* const CostumeMesh = GetSkelMeshComponent())
	{
		CostumeMesh->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		CostumeMesh->SetUpdateAnimationInEditor(true);
	}

	ApplyCostumePresentationFixes();
}

void UGodfreyCostumeRetargetAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const bool bHadSource = SourceMeshComponent != nullptr;
	const bool bHadAsset = IKRetargeterAsset != nullptr;
	ResolveSourceAndRetargeter();

	const bool bNewlyResolved =
		(!bHadSource && SourceMeshComponent != nullptr)
		|| (!bHadAsset && IKRetargeterAsset != nullptr);

	if (bNewlyResolved || !bRetargetConfigured)
	{
		EnsureCostumeTicksAfterSource();
		const bool bChanged = PushRetargetConfigToProxy();
		ForceProcessorReady();
		bRetargetConfigured = (IKRetargeterAsset != nullptr && SourceMeshComponent != nullptr);
		if (bChanged)
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyCostumeRetarget: configured retargeter=%s source=%s"),
				IKRetargeterAsset ? *IKRetargeterAsset->GetName() : TEXT("null"),
				SourceMeshComponent ? *SourceMeshComponent->GetName() : TEXT("null"));
		}
	}
	else
	{
		// Keep processor alive without SetNeedsInitialized (that empties PreUpdate's pose copy).
		FIKRetargetProcessor* const Proc = RetargetNode.GetRetargetProcessor();
		if (!Proc || !Proc->IsInitialized())
		{
			ForceProcessorReady();
		}
	}

	ApplyCostumePresentationFixes();

	if (DiagnosticFramesRemaining > 0)
	{
		--DiagnosticFramesRemaining;
		FIKRetargetProcessor* const Proc = RetargetNode.GetRetargetProcessor();
		const bool bInit = Proc && Proc->IsInitialized();
		const int32 SourceBones = bInit
			? Proc->GetSkeleton(ERetargetSourceOrTarget::Source).BoneNames.Num()
			: -1;
		const int32 BodyCS = SourceMeshComponent
			? SourceMeshComponent->GetComponentSpaceTransforms().Num()
			: -1;
		if (DiagnosticFramesRemaining == 29 || DiagnosticFramesRemaining == 0 || !bInit)
		{
			UE_LOG(LogGodfreyPerformance, Warning,
				TEXT("GodfreyCostumeRetarget init=%d srcBones=%d bodyCS=%d sourceComp=%s asset=%s configured=%d"),
				bInit ? 1 : 0,
				SourceBones,
				BodyCS,
				SourceMeshComponent ? *SourceMeshComponent->GetName() : TEXT("null"),
				IKRetargeterAsset ? *IKRetargeterAsset->GetName() : TEXT("null"),
				bRetargetConfigured ? 1 : 0);
		}
	}

	if (!bLoggedReady && IKRetargeterAsset && SourceMeshComponent)
	{
		bLoggedReady = true;
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyCostumeRetarget: ready retargeter=%s source=%s costume=%s"),
			*IKRetargeterAsset->GetName(),
			*SourceMeshComponent->GetName(),
			GetSkelMeshComponent() ? *GetSkelMeshComponent()->GetName() : TEXT("(none)"));
	}
}
