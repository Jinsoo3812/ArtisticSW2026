#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SWCorpsePlaygroundMode.generated.h"
class ACameraActor;

/** Standalone PIE test-map policy. Does not replace the production GameMode. */
UCLASS()
class CLASSFEATUREEDITOR_API ASWCorpsePlaygroundMode:public AGameModeBase
{
    GENERATED_BODY()
public:
    ASWCorpsePlaygroundMode();
    virtual void Tick(float DeltaSeconds) override;
    UFUNCTION(BlueprintCallable,Category="Corpse Playground") void TriggerDeath();
    UFUNCTION(BlueprintCallable,Category="Corpse Playground") void SwitchGender();
    /** Exercise the bound chord handler without injecting keyboard input into Windows. */
    UFUNCTION(BlueprintCallable,Category="Corpse Playground") bool ExecuteTestChord(bool bGenderSwitch);
    UFUNCTION(BlueprintPure,Category="Corpse Playground") float GetRespawnRemaining() const;
    UPROPERTY(VisibleAnywhere,Category="Corpse Playground") int32 ConfirmedRespawns=0;
    UPROPERTY(VisibleAnywhere,Category="Corpse Playground") FString LastError;
    UPROPERTY(VisibleAnywhere,Category="Corpse Playground") bool bFemale=false;
    UPROPERTY(EditAnywhere,Category="Corpse Playground") FVector RespawnLocation=FVector(0,0,1094);
private:
    FTimerHandle RespawnTimer;
    bool bBoundInput=false,bPending=false,bLockedInput=false,bSavedAutoCamera=true;
    UPROPERTY() TObjectPtr<ACameraActor> DeathCamera;
    FVector CameraOffset;
    FRotator CameraRotation;
    void BeginCountdown();
    void Respawn();
};
