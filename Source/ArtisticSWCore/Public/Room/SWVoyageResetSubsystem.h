#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "Room/SWVoyageResetTypes.h"
#include "SWVoyageResetSubsystem.generated.h"

class ULevelStreaming;
class ULevel;
class USWVoyageResetProfile;
class APlayerState;

UCLASS()
class ARTISTICSWCORE_API USWVoyageResetSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	bool ConfigureFromAnchor(FString& OutError);
	bool BeginBootstrapPreparation(int32 Generation, FString& OutError);
	bool BeginLocalPhase(const FSWVoyageResetContext& Context, FString& OutError);
	ESWVoyageStepResult PollLocalPhase(FString& OutError);
	bool BeginRestoreStage(ESWVoyageRestoreStage Stage, FString& OutError);
	ESWVoyageStepResult PollRestoreStage(FString& OutError);
	bool ValidateVoyageContracts(FString& OutError) const;
	bool RegisterParticipant(UObject* Object, FString& OutError);
	FName ResolveParticipantId(UObject* Object);
	bool IsActiveVoyageSession() const;
	bool AdoptInitialClientGeneration(const FSWVoyageReplicatedState& State, const FGuid& RoomRunId, FString& OutError);
	bool CanSpawnVoyageActor(ESWVoyageActorLifetime Lifetime, int32 ExpectedGeneration) const;
	bool CanSpawnVoyageActor(ESWVoyageActorLifetime Lifetime, int32 ExpectedGeneration, const FGuid& PresentationCleanupId) const;
	UFUNCTION(BlueprintCallable, Category="Voyage")
	bool RegisterLocalPresentationCleanupOwner(UObject* CleanupOwner, int32 ExpectedGeneration, FGuid& OutCleanupId, FString& OutError);
	UFUNCTION(BlueprintCallable, Category="Voyage")
	void UnregisterLocalPresentationCleanupOwner(FGuid CleanupId);
	bool GetActorPresentationCleanupId(const AActor* Actor, FGuid& OutCleanupId) const;
	void UnregisterParticipant(UObject* Object);
	bool RegisterActor(AActor* Actor, ESWVoyageActorLifetime Lifetime, int32 Generation, FString& OutError);
	bool RegisterActor(AActor* Actor, ESWVoyageActorLifetime Lifetime, int32 Generation, const FGuid& PresentationCleanupId, FString& OutError);
	void UnregisterActor(AActor* Actor);
	bool IsCurrentGeneration(int32 Generation) const;
	bool IsGameplayBlocked() const;
	bool IsPreparationSpawnAllowed() const;
	int32 GetGeneration() const;
	int32 GetActorGeneration(const AActor* Actor) const;
	bool GetActorLifetime(const AActor* Actor, ESWVoyageActorLifetime& OutLifetime) const;
	ULevelStreaming* GetGameplayStreamingLevel() const;
	void SetPreparationSpawnAllowed(bool bAllowed);
	TSharedPtr<FSWVoyageAsyncGuard, ESPMode::ThreadSafe> GetAsyncGuard();
private:
	TSharedPtr<FSWVoyageAsyncGuard, ESPMode::ThreadSafe> AsyncGuard;
	void UpdateAsyncGuard();
	struct FActorRegistration
	{
		ESWVoyageActorLifetime Lifetime = ESWVoyageActorLifetime::Environment;
		int32 Generation = 0;
		FGuid PresentationCleanupId;
	};
	struct FPresentationCleanupRegistration
	{
		TWeakObjectPtr<UObject> Owner;
		int32 Generation = 0;
	};
	TMap<FGuid, FPresentationCleanupRegistration> PresentationCleanupOwners;
	UPROPERTY(Transient) TObjectPtr<USWVoyageResetProfile> Profile;
	UPROPERTY(Transient) FSWVoyageResetContext Context;
	UPROPERTY(Transient) TObjectPtr<APlayerState> LocalPauseSentinel;
	TWeakObjectPtr<APlayerState> PreviousPauser;
	TWeakObjectPtr<ULevelStreaming> GameplayStreaming;
	TMap<TWeakObjectPtr<AActor>, FActorRegistration> Actors;
	TSet<TWeakObjectPtr<UObject>> Participants;
	TMap<TWeakObjectPtr<UObject>, FGuid> ParticipantRuntimeIds;
	TArray<TWeakObjectPtr<UObject>> OrderedParticipants;
	TSet<TWeakObjectPtr<UObject>> CompletedParticipants;
	TSet<TWeakObjectPtr<UObject>> PreparedParticipants;
	TSet<TWeakObjectPtr<UObject>> ResumedParticipants;
	TArray<TWeakObjectPtr<AActor>> OldActors;
	TWeakObjectPtr<ULevel> OldLevel;
	TSet<uint32> OldStaticGuids;
	FDelegateHandle PostGcHandle;
	FDelegateHandle ActorSpawnedHandle;
	FTSTicker::FDelegateHandle CoreTickerHandle;
	uint64 ObservedGcSerial = 0;
	uint64 RequiredGcSerial = 0;
	int32 ActorGeneration = 0;
	FGuid InitialClientRoomRunId;
	int32 InitialClientGeneration = 0;
	bool bActive = false;
	bool bBlocked = false;
	bool bPreparationSpawnAllowed = false;
	bool bDestructiveStarted = false;
	bool bUnloadIssued = false;
	bool bRestoreStageStarted = false;
	bool bRestoreStageCompleted = false;
	FString RegistrationFailure;
	void HandlePostGarbageCollect();
	void HandleActorSpawned(AActor* Actor);
	bool Tick(float DeltaSeconds);
	void MaintainLocalPause();
	void RestoreLocalPause();
	void DiscoverParticipants(TArray<UObject*>& OutObjects) const;
	bool BuildParticipantOrder(FString& OutError);
	bool CollectValidatedParticipantOrder(TArray<TWeakObjectPtr<UObject>>& OutOrder, FString& OutError) const;
	ESWVoyageStepResult PollParticipants(FString& OutError);
	bool AuditWorldActors(FString& OutError);
};
