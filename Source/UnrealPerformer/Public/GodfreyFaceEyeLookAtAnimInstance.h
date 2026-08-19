#pragma once

#include "UnrealPerformerApi.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Root.h"
#include "Animation/AnimNode_LinkedInputPose.h"
#include "AnimNodes/AnimNode_ModifyCurve.h"
#include "GodfreyFaceEyeLookAtAnimInstance.generated.h"

/**
 * Face-mesh post-process: blends MetaHuman eyeLook expression curves toward Exhibit_CineCamera.
 * Curve-only — no bone LookAt or component-space conversions (those break body/face alignment).
 */
USTRUCT()
struct FGodfreyFaceEyeLookAtAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

public:
	FGodfreyFaceEyeLookAtAnimInstanceProxy() = default;
	explicit FGodfreyFaceEyeLookAtAnimInstanceProxy(UAnimInstance* InAnimInstance);

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual FAnimNode_Base* GetCustomRootNode() override;
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

private:
	void ConstructNodes();
	void UpdateEyeLookCurves(const UGodfreyFaceEyeLookAtAnimInstance* FaceInst);

	FAnimNode_LinkedInputPose InputPoseNode;
	FAnimNode_ModifyCurve ModifyCurveNode;
	FAnimNode_Root RootNode;
};

UCLASS(BlueprintType, Blueprintable)
class UNREAL_PERFORMER_API UGodfreyFaceEyeLookAtAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UGodfreyFaceEyeLookAtAnimInstance(const FObjectInitializer& ObjectInitializer);

	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|EyeLookAt")
	bool bEnableEyeLookAt = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|EyeLookAt")
	FVector EyeLookAtWorldLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|EyeLookAt", meta = (ClampMin = "0", ClampMax = "1"))
	float EyeLookAtAlpha = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|EyeLookAt")
	FName HeadBoneNameForCurveAim = FName(TEXT("head"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|EyeLookAt", meta = (ClampMin = "5", ClampMax = "45"))
	float EyeLookAtMaxYawDegrees = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|EyeLookAt", meta = (ClampMin = "5", ClampMax = "45"))
	float EyeLookAtMaxPitchDegrees = 22.f;

private:
	bool bLoggedEnable = false;
};
