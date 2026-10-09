#pragma once

#include "CoreMinimal.h"
#include "BossSlashDashTypes.generated.h"

class UAnimMontage;

/**
 * Authoring contract for the server-driven DashSlash montage phases.
 * Gameplay timing is derived from these sections and never depends on AnimNotifies.
 */
USTRUCT(BlueprintType)
struct ENEMY_API FDashSlashMontageConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	FName WindupEnterSectionName = TEXT("Windup");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	FName WindupHoldSectionName = TEXT("WindupHold");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	FName AttackSectionName = TEXT("DashSlash");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	FName TravelHoldSectionName = TEXT("DashHold");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	FName RecoverySectionName = TEXT("Recover");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage", meta = (ClampMin = "0.01"))
	float PlayRate = 1.0f;

	/** Time spent in the looping WindupHold section, excluding WindupEnter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage", meta = (ClampMin = "0.0", Units = "s"))
	float WindupHoldDuration = 0.5f;

	/** Fails safe if the authored Recover section never completes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage", meta = (ClampMin = "0.1", Units = "s"))
	float RecoveryTimeout = 1.5f;
};

enum class EDashSlashPhase : uint8
{
	Inactive,
	WindupEntering,
	WindupHolding,
	DashAttacking,
	WaitingForCompletion,
	Recovering
};
