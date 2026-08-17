#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NPCDialogueTypes.h"
#include "NPCDialogueData.generated.h"

/** Designer-authored identity, ordered lines, story conditions, and typed outcomes for one NPC. */
UCLASS(BlueprintType, Const)
class NPCDIALOGUE_API UNPCDialogueData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC")
	FText InteractionActionText = NSLOCTEXT("NPCDialogue", "TalkAction", "Talk");

	/** Position is authored on BP_NPC's DialogueCameraAnchor; these tune presentation only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "5.0", ClampMax = "170.0"))
	float CameraFieldOfView = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float CameraBlendInTime = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float CameraBlendOutTime = 0.25f;

	/** World-space offset from the NPC origin used as the camera look-at point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	FVector CameraLookAtOffset = FVector(0.0, 0.0, 120.0);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dialogue", meta = (TitleProperty = "RuleId"))
	TArray<FNPCDialogueRule> Rules;

	const FNPCDialogueRule* FindRule(FName RuleId) const;

	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
};
