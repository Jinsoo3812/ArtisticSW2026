#include "Development/SWCorpsePlaygroundMode.h"
#include "MultiGameMode.h"
#include "Components/SWCorpsePoseComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Framework/Commands/InputChord.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
UActorComponent* Health(APawn* Pawn)
{
    if(!Pawn)return nullptr;
    for(UActorComponent* C:Pawn->GetComponents())if(C&&C->FindFunction(TEXT("StartDeath"))&&C->FindFunction(TEXT("IsDead")))return C;
    return nullptr;
}
bool Dead(APawn* Pawn)
{
    UActorComponent* H=Health(Pawn);if(!H)return false;
    UFunction* F=H->FindFunction(TEXT("IsDead"));FStructOnScope P(F);H->ProcessEvent(F,P.GetStructMemory());
    const auto* Return=FindFProperty<FBoolProperty>(F,TEXT("ReturnValue"));return Return&&Return->GetPropertyValue_InContainer(P.GetStructMemory());
}
}

ASWCorpsePlaygroundMode::ASWCorpsePlaygroundMode(){PrimaryActorTick.bCanEverTick=true;}
float ASWCorpsePlaygroundMode::GetRespawnRemaining() const{return bPending?FMath::Max(0.f,GetWorldTimerManager().GetTimerRemaining(RespawnTimer)):0.f;}
void ASWCorpsePlaygroundMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    APlayerController* PC=GetWorld()->GetFirstPlayerController();if(!PC||!PC->IsLocalController())return;
    if(!bBoundInput)
    {
        EnableInput(PC);InputComponent->Priority=100;
        InputComponent->BindKey(FInputChord(EKeys::F10,true,true,true,false),IE_Pressed,this,&ASWCorpsePlaygroundMode::TriggerDeath).bConsumeInput=true;
        InputComponent->BindKey(FInputChord(EKeys::F11,true,true,true,false),IE_Pressed,this,&ASWCorpsePlaygroundMode::SwitchGender).bConsumeInput=true;
        bBoundInput=true;
    }
    if(!bPending&&Dead(PC->GetPawn()))BeginCountdown();
    if(bPending&&DeathCamera)
        if(ACharacter* C=Cast<ACharacter>(PC->GetPawn()))DeathCamera->SetActorLocationAndRotation(C->GetMesh()->GetSocketLocation(TEXT("pelvis"))+CameraOffset,CameraRotation);
    if(GEngine)
    {
        FString Text=TEXT("CORPSE TEST  |  Move normally  |  Ctrl+Alt+Shift+F10: Death  |  Ctrl+Alt+Shift+F11: Switch Man/Woman");
        if(bPending)Text+=FString::Printf(TEXT("  |  Respawn %.1fs"),GetRespawnRemaining());
        if(!LastError.IsEmpty())Text+=TEXT("  |  ")+LastError;
        GEngine->AddOnScreenDebugMessage(0xC011,0.f,bPending?FColor::Orange:FColor::Cyan,Text);
    }
}
void ASWCorpsePlaygroundMode::TriggerDeath()
{
    if(APlayerController* PC=GetWorld()->GetFirstPlayerController())
    {
        if(!Dead(PC->GetPawn()))if(UActorComponent* H=Health(PC->GetPawn()))H->ProcessEvent(H->FindFunction(TEXT("StartDeath")),nullptr);
        if(Dead(PC->GetPawn())&&!bPending)BeginCountdown();
    }
}
void ASWCorpsePlaygroundMode::BeginCountdown()
{
    APlayerController* PC=GetWorld()->GetFirstPlayerController();ACharacter* C=PC?Cast<ACharacter>(PC->GetPawn()):nullptr;if(!C)return;
    bPending=true;LastError.Reset();
    C->SetLifeSpan(AMultiGameMode::IndividualRespawnDelay + 2.f);
    if(PC->PlayerCameraManager)
    {
        const FMinimalViewInfo& POV=PC->PlayerCameraManager->GetCameraCacheView();
        CameraOffset=POV.Location-C->GetMesh()->GetSocketLocation(TEXT("pelvis"));CameraRotation=POV.Rotation;
        if(!DeathCamera)DeathCamera=GetWorld()->SpawnActor<ACameraActor>();
        if(DeathCamera){DeathCamera->SetActorLocationAndRotation(POV.Location,CameraRotation);DeathCamera->GetCameraComponent()->SetFieldOfView(POV.FOV);}
    }
    bSavedAutoCamera=PC->bAutoManageActiveCameraTarget;PC->bAutoManageActiveCameraTarget=false;
    if(DeathCamera)PC->SetViewTargetWithBlend(DeathCamera,.15f);
    PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true);bLockedInput=true;
    GetWorldTimerManager().SetTimer(RespawnTimer,this,&ASWCorpsePlaygroundMode::Respawn,AMultiGameMode::IndividualRespawnDelay,false);
}
void ASWCorpsePlaygroundMode::SwitchGender(){bFemale=!bFemale;TriggerDeath();if(bPending)Respawn();}
bool ASWCorpsePlaygroundMode::ExecuteTestChord(bool bGenderSwitch)
{
    if(!InputComponent)return false;
    const FKey Key=bGenderSwitch?EKeys::F11:EKeys::F10;
    for(FInputKeyBinding& Binding:InputComponent->KeyBindings)
        if(Binding.Chord.Key==Key&&Binding.Chord.bCtrl&&Binding.Chord.bAlt&&Binding.Chord.bShift&&!Binding.Chord.bCmd&&Binding.KeyEvent==IE_Pressed)
        {Binding.KeyDelegate.Execute(Key);return true;}
    return false;
}
void ASWCorpsePlaygroundMode::Respawn()
{
    APlayerController* PC=GetWorld()->GetFirstPlayerController();if(!PC)return;
    if(GetWorld()->WorldType!=EWorldType::PIE){LastError=TEXT("Use Play In Editor for the production life-restore fixture");return;}
    UClass* Library=LoadObject<UClass>(nullptr,TEXT("/Script/ClassFeatureEditor.SWCharacterRigValidationLibrary"));
    UFunction* Function=Library?Library->FindFunctionByName(TEXT("SpawnRagdollTestNewLife")):nullptr;
    UBlueprint* BP=LoadObject<UBlueprint>(nullptr,bFemale?TEXT("/Game/Blueprints/Player/BP_Player_Woman.BP_Player_Woman"):TEXT("/Game/Blueprints/Player/BP_Player_Man.BP_Player_Man"));
    if(!Function||!BP){LastError=TEXT("Missing editor life-restore helper or player BP");return;}
    FStructOnScope Params(Function);uint8* Memory=Params.GetStructMemory();
    FindFProperty<FObjectPropertyBase>(Function,TEXT("Controller"))->SetObjectPropertyValue_InContainer(Memory,PC);
    FindFProperty<FObjectPropertyBase>(Function,TEXT("Blueprint"))->SetObjectPropertyValue_InContainer(Memory,BP);
    *FindFProperty<FStructProperty>(Function,TEXT("Location"))->ContainerPtrToValuePtr<FVector>(Memory)=RespawnLocation;
    Library->GetDefaultObject()->ProcessEvent(Function,Memory);
    ACharacter* Next=Cast<ACharacter>(FindFProperty<FObjectPropertyBase>(Function,TEXT("ReturnValue"))->GetObjectPropertyValue_InContainer(Memory));
    if(!Next){LastError=TEXT("Life restore failed; inspect the private project log");return;}
    GetWorldTimerManager().ClearTimer(RespawnTimer);bPending=false;++ConfirmedRespawns;
    if(bLockedInput){PC->SetIgnoreMoveInput(false);PC->SetIgnoreLookInput(false);bLockedInput=false;}
    PC->bAutoManageActiveCameraTarget=bSavedAutoCamera;PC->SetViewTargetWithBlend(Next,.15f);LastError.Reset();
}
