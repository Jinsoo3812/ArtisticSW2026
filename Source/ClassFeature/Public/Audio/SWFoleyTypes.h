#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "SWFoleyTypes.generated.h"

UENUM(BlueprintType)
enum class ESWFoleySide : uint8 { None, Left, Right };

/** Shared admission/cooldown families: changing Walk to Run must not double a contact. */
UENUM(BlueprintType)
enum class ESWFoleyGroup : uint8 { Footstep, Scuff, Jump, Land, Other };

/** Contact location policy is independent of the animation/event naming convention. */
UENUM(BlueprintType)
enum class ESWFoleyContact : uint8 { Foot, Hand, Capsule, Socket };

USTRUCT(BlueprintType)
struct CLASSFEATURE_API FSWFoleyEvent
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foley")
	FGameplayTag Event;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foley")
	ESWFoleySide Side = ESWFoleySide::None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foley", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foley", meta = (ClampMin = "0.01"))
	float PitchMultiplier = 1.0f;
	/** Age of a confirmed semantic event. Old replication snapshots must stay silent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foley", meta = (ClampMin = "0.0"))
	float AgeSeconds = 0.0f;
};

namespace SW::Foley
{
	CLASSFEATURE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Walk);
	CLASSFEATURE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Run);
	CLASSFEATURE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Jump);
	CLASSFEATURE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Land);
	CLASSFEATURE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Scuff);
	CLASSFEATURE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Handplant);
	CLASSFEATURE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(RunBackwards);
	CLASSFEATURE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(ScuffPivot);
	CLASSFEATURE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(ScuffWall);
	CLASSFEATURE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(RunStrafe);
	CLASSFEATURE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tumble);
	CLASSFEATURE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(WalkBackwards);
}
