#pragma once

#include "UnrealPerformerApi.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Root.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "GodfreyCostumeRetargetAnimInstance.generated.h"

class UIKRetargeter;
class USkeletalMeshComponent;

USTRUCT()
struct FGodfreyCostumeRetargetAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

public:
	FGodfreyCostumeRetargetAnimInstanceProxy() = default;
	FGodfreyCostumeRetargetAnimInstanceProxy(
		UAnimInstance* InAnimInstance,
		FAnimNode_Root* InRootNode,
		FAnimNode_RetargetPoseFromMesh* InRetargetNode);

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual FAnimNode_Base* GetCustomRootNode() override;
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

	/** Returns true if retarget config changed (and processor was marked dirty). */
	bool ConfigureRetarget(
		UIKRetargeter* InRetargeter,
		USkeletalMeshComponent* InSourceMesh);

private:
	FAnimNode_Root* RootNode = nullptr;
	FAnimNode_RetargetPoseFromMesh* RetargetNode = nullptr;
};

/**
 * Drives Genesis costume from MetaHuman Body via IK Retarget Pose From Mesh.
 * Node storage matches Epic's UIKRetargetAnimInstance (UPROPERTY on the UObject).
 */
UCLASS(BlueprintType, Blueprintable)
class UNREAL_PERFORMER_API UGodfreyCostumeRetargetAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UGodfreyCostumeRetargetAnimInstance(const FObjectInitializer& ObjectInitializer);

	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Costume|Retarget")
	FSoftObjectPath DefaultIKRetargeterPath =
		FSoftObjectPath(TEXT("/Game/MetaHumans/Costume/Retargeting/RTG_MetaHuman_To_Victorian.RTG_MetaHuman_To_Victorian"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Costume|Retarget")
	TObjectPtr<UIKRetargeter> IKRetargeterAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Costume|Retarget")
	TObjectPtr<USkeletalMeshComponent> SourceMeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Costume|Retarget")
	FName SourceMeshComponentName = FName(TEXT("Body"));

private:
	void ResolveSourceAndRetargeter();
	bool PushRetargetConfigToProxy();
	void ForceProcessorReady();
	void EnsureCostumeTicksAfterSource();
	void ApplyCostumePresentationFixes();

	/** Owned by AnimInstance (Epic pattern) — not the proxy. */
	UPROPERTY(Transient)
	FAnimNode_RetargetPoseFromMesh RetargetNode;

	UPROPERTY(Transient)
	FAnimNode_Root RootNode;

	bool bLoggedReady = false;
	bool bCostumeMaterialsRestored = false;
	bool bCostumeTransformFixed = false;
	bool bRetargetConfigured = false;
	int32 DiagnosticFramesRemaining = 30;
};
