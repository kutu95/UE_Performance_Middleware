#pragma once

#include "UnrealPerformerApi.h"
#include "Components/ActorComponent.h"
#include "GodfreyRuntimePerfHudComponent.generated.h"

/**
 * Development-only on-screen Godfrey performance overlay (FPS, speech latency, SpeechId, behaviour).
 * Toggle with Project Settings → GodfreyPerfHudToggleKey (default F8). No gameplay side effects.
 */
UCLASS(ClassGroup = (Godfrey), meta = (BlueprintSpawnableComponent))
class UNREAL_PERFORMER_API UGodfreyRuntimePerfHudComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGodfreyRuntimePerfHudComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Diagnostics|HUD")
	bool bStartVisible = true;

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Diagnostics|HUD")
	void SetHudVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "Godfrey|Diagnostics|HUD")
	bool IsHudVisible() const { return bHudVisible; }

private:
	void DrawHud() const;
	bool bHudVisible = true;
};
