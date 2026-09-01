#pragma once

#include "UnrealPerformerApi.h"
#include "Components/ActorComponent.h"
#include "GodfreyRuntimePerfHudComponent.generated.h"

class STextBlock;
class SWidget;
struct FSlateColorBrush;
class UGodfreyPerformerAnimationBridgeComponent;

/**
 * Development-only on-screen Godfrey overlay:
 * - F8: FPS / speech latency line (no gameplay side effects).
 * - Always-on (F6 toggles) top-left box with the current body AnimSequence name.
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
	void EnsureAnimOverlay();
	void TearDownAnimOverlay();
	void UpdateAnimOverlay();
	UGodfreyPerformerAnimationBridgeComponent* ResolveBridge() const;

	bool bHudVisible = true;
	bool bAnimOverlayVisible = true;
	bool bAnimOverlayAdded = false;

	TSharedPtr<SWidget> AnimOverlayWidget;
	TSharedPtr<STextBlock> AnimNameText;
	TSharedPtr<STextBlock> AnimContextText;
	TSharedPtr<FSlateColorBrush> AnimBackingBrush;
	mutable TWeakObjectPtr<UGodfreyPerformerAnimationBridgeComponent> CachedBridge;
};
