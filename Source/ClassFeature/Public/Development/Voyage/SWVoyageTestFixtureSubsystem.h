#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Room/SWVoyageResetParticipant.h"
#include "SWVoyageTestFixtureSubsystem.generated.h"

class ABasePlayerController;
class ASharedStorageChest;
class ASharedShipUpgradeState;

/** Opt-in isolated Development fixtures; shared room progress owns their durable data. */
UCLASS()
class CLASSFEATURE_API USWVoyageTestFixtureSubsystem : public UWorldSubsystem, public ISWVoyageResetParticipant
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual ESWVoyagePolicy GetVoyagePolicy_Implementation() const override { return ESWVoyagePolicy::Preserve; }
	virtual FName GetVoyageParticipantId_Implementation() const override { return TEXT("SWVoyageTestFixture"); }
	virtual void ResumeVoyage_Implementation(const FSWVoyageResetContext& Context) override;

	/** Called only by the existing authenticated idle host Development probe. */
	bool Seed(ABasePlayerController* Requester, FString& OutError);
	bool Observe(ABasePlayerController* Requester, FString& OutError) const;

private:
	void LogObservation(const TCHAR* Event) const;
	TWeakObjectPtr<ASharedStorageChest> FixtureChest;
	TWeakObjectPtr<ASharedShipUpgradeState> FixtureUpgrade;
	bool bSeeded = false;
};
