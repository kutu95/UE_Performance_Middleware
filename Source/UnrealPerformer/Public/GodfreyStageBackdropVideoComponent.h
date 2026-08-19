#pragma once

#include "UnrealPerformerApi.h"
#include "Components/ActorComponent.h"
#include "GodfreyStageBackdropVideoComponent.generated.h"

class UMediaPlayer;
class UMediaTexture;
class UStaticMeshComponent;

/** Loops an mp4 on MediaPlate2 (harbour backdrop). Extra MediaPlates and Stage_Backdrop are hidden in play. */
UCLASS(ClassGroup = (Godfrey), meta = (BlueprintSpawnableComponent))
class UNREAL_PERFORMER_API UGodfreyStageBackdropVideoComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGodfreyStageBackdropVideoComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void StartBackdropVideo();
	void StopBackdropVideo();
	bool TryPlayOnMediaPlate(const FString& FullPath);
	void SizeMediaPlateToFillPortraitHeight(AActor* Plate);
	void ApplyTextureToBackdrop(UStaticMeshComponent* Mesh);
	void HideExtraMediaPlateActors(AActor* KeepVisible);
	void HideActorByLabel(const TCHAR* Label, bool bDisableCollision);
	void HideExhibitFloorInPlay();

	UFUNCTION()
	void HandleMediaOpened(FString OpenedUrl);

	UFUNCTION()
	void HandleMediaOpenFailed(FString FailedUrl);

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> MediaPlayer;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> MediaTexture;
};
