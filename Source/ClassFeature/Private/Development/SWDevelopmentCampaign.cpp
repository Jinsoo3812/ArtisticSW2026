#include "Room/ClassFeatureRoomProgressSubsystem.h"

#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "Cannon.h"
#include "KelvinShip.h"
#include "MultiGameMode.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/InventoryComponent.h"
#include "Item/ItemSubsystem.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "Room/SWRoomReadyState.h"
#include "Room/SWLevelEntryPoint.h"
#include "Room/SWFinalEncounterShipEntryPoint.h"
#include "Skills/PlayerSkillComponent.h"
#include "StoryFacadeSubsystem.h"
#include "StorySaveGame.h"
#include "Storage/StorageChest.h"
#include "Storage/StorageComponent.h"

bool UClassFeatureRoomProgressSubsystem::PrepareDevelopmentCampaign(UWorld* World, FString& OutError)
{
 USWRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>();
 AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
 UStoryFacadeSubsystem* Story = GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>();
 UItemSubsystem* Items = World ? World->GetSubsystem<UItemSubsystem>() : nullptr;
 UStorySaveGame* PreparedStory = Story ? Story->BuildDevelopmentCheckpoint(CampaignTarget, OutError) : nullptr;
 if (!Room || !Room->GetActiveRoom() || !Mode || !Items || !PreparedStory) return false;
 const int32 Index = UStoryFacadeSubsystem::GetDevelopmentCheckpointIndex(CampaignTarget);
 TArray<FGameplayTag> Rewards = { Item_Quest_CipherBook };
 if (Index >= 1) Rewards.Add(Item_Quest_InvasionMap);
 if (Index >= 2) Rewards.Add(Item_Quest_JapaneseCipher);
 if (Index >= 3) Rewards.Add(Item_Quest_DecipheredCipher);
 if (Index >= 4) Rewards.Add(Item_Quest_AirRaidInfo);
 for (const FGameplayTag Tag : Rewards)
  if (!Items->GetItemDefinition(Tag)) { OutError = FString::Printf(TEXT("Missing item definition: %s"), *Tag.ToString()); return false; }
 if (!CaptureSharedWorld(World)) { OutError = TEXT("Cannot capture shared room progress."); return false; }
 CampaignPreparedRoom = DuplicateObject<USWRoomSaveGame>(Room->GetActiveRoom(), this);
 FSWRoomSharedProgress& Shared = CampaignPreparedRoom->SharedProgress;
 Shared.StoryDefinitionId = PreparedStory->StoryDefinitionId;
 Shared.Facts = PreparedStory->Facts;
 Shared.AppliedActionKeys = PreparedStory->AppliedActionKeys;
 Shared.Counters.Reset();
 for (const FStoryCounterValue& Value : PreparedStory->Counters)
 { FSWRoomCounterProgress& Counter = Shared.Counters.AddDefaulted_GetRef(); Counter.Tag = Value.CounterTag; Counter.Value = Value.Value; }
 int32 PlayerCount = 0;
 bool bHostFound = false;
 TSet<FString> GuestNames;
 for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
 {
  ABasePlayerController* PC = Cast<ABasePlayerController>(It->Get());
  ABasePlayer* Player = PC ? PC->GetLifeCharacter() : nullptr;
  UInventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
  UPlayerSkillComponent* Skills = Player ? Player->GetPlayerSkillComponent() : nullptr;
  if (!PC || !PC->IsLifeCharacterAlive() || !Inventory || !Skills || Player->GetController() != PC || !PC->PlayerState)
  { OutError = TEXT("All connected players must be alive, restored, and dismounted."); return false; }
  FSWRoomPlayerProgress Progress;
  Player->CaptureRoomProgress(Progress);
  if (!Progress.CaptureIssues.IsEmpty()) { OutError = TEXT("Player capture has unresolved issues."); return false; }
  TArray<FCraftingItemStack> Grants;
  for (const FGameplayTag Tag : Rewards)
   if (Inventory->GetItemCount(Tag) < 1)
   { FCraftingItemStack& Grant = Grants.AddDefaulted_GetRef(); Grant.ItemTag = Tag; Grant.Quantity = 1; }
  if (!Inventory->BuildSnapshotWithAddedItems(Grants, Progress.InventorySlots))
  { OutError = FString::Printf(TEXT("Not enough inventory space: %s"), *PC->PlayerState->GetPlayerName()); return false; }
  auto Unlock = [&Progress, Skills](FGameplayTag Tag)
  {
   if (!Skills->FindSkillDefinition(Tag)) return false;
   FSWRoomSkillProgress* Skill = Progress.Skills.FindByPredicate([Tag](const FSWRoomSkillProgress& Entry) { return Entry.SkillTag == Tag; });
   if (!Skill) { Skill = &Progress.Skills.AddDefaulted_GetRef(); Skill->SkillTag = Tag; }
   Skill->bUnlocked = true; Skill->bConditionsMet = true;
   return true;
  };
  if ((Index >= 1 && !Unlock(GameplayAbility_Skill_GravityVortex))
   || (Index >= 3 && !Unlock(GameplayAbility_Skill_WaterBomb))
   || (Index >= 4 && !Unlock(GameplayAbility_Skill_Bombardment)))
  { OutError = TEXT("Required campaign skill definition is missing."); return false; }
  if (Mode->GetPlayerIndex(PC) == 0)
  { if (bHostFound) return false; bHostFound = true; CampaignPreparedRoom->HostProgress = Progress; }
  else
  {
   const FString Name = PC->PlayerState->GetPlayerName();
   if (Name.IsEmpty() || GuestNames.Contains(Name)) { OutError = TEXT("Guest identity is ambiguous."); return false; }
   GuestNames.Add(Name);
   FSWRoomGuestProgress* Guest = CampaignPreparedRoom->Guests.FindByPredicate([&Name](const FSWRoomGuestProgress& Entry) { return Entry.DisplayName == Name; });
   if (!Guest) { Guest = &CampaignPreparedRoom->Guests.AddDefaulted_GetRef(); Guest->DisplayName = Name; }
   Guest->Progress = Progress;
  }
  ++PlayerCount;
 }
 if (!bHostFound || PlayerCount != ReturnControllers.Num()) { OutError = TEXT("Participant list changed."); return false; }
 CampaignPreparedRoom->bFinalDepartureCompleted = Index >= 5;
 return true;
}

bool UClassFeatureRoomProgressSubsystem::TryDevelopmentCampaignSkip(UWorld* World,
 ABasePlayerController* Requester, FName Target, FString& OutError)
{
 OutError = TEXT("Campaign skip requires a ready development session and the host.");
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
 USWRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>();
 AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
 ASWRoomReadyState* Ready = nullptr;
 if (World) for (TActorIterator<ASWRoomReadyState> It(World); It; ++It) { Ready = *It; break; }
 if (!Room || !Room->IsHostedRoom() || !Room->GetActiveRoom() || !Mode || !Requester
  || !Requester->HasAuthority() || !Mode->IsRoomHostController(Requester)
  || !Room->IsDevelopmentTestSessionEnabled(World) || !Ready || !Ready->bWorldReady
  || Ready->RestoreGeneration != Room->GetRestoreGeneration() || IsDevelopmentTransitionBusy()
  || Mode->IsLevelRestartRequested() || Mode->GetSessionLifePhase() != ESWSessionLifePhase::Playing
  || Room->IsReturnTravelPending() || Room->IsFinalDepartureTravelPending() || Room->IsGameOverTravelPending()
  || Room->IsGameOverRetryTravelPending() || Room->IsNewRoomPending()) return false;
 const int32 Index = UStoryFacadeSubsystem::GetDevelopmentCheckpointIndex(Target);
 UStoryFacadeSubsystem* Story = GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>();
 if (Index == INDEX_NONE || !Story || !Story->BuildDevelopmentCheckpoint(Target, OutError)) return false;
 int32 EntryCounts[3] = {0, 0, 0}; int32 FinalEntries = 0; int32 Ships = 0;
 for (TActorIterator<ASWLevelEntryPoint> It(World); It; ++It)
 { const int32 Role = static_cast<int32>(It->EntryRole); if (Role >= 0 && Role < 3) ++EntryCounts[Role]; }
 for (TActorIterator<ASWFinalEncounterShipEntryPoint> It(World); It; ++It) ++FinalEntries;
 for (TActorIterator<AKelvinShip> It(World); It; ++It) if (!It->ActorHasTag(TEXT("Enemy"))) ++Ships;
 if (Ships != 1 || (Index == 4 ? FinalEntries != 1 : EntryCounts[0] != 1 || EntryCounts[1] != 1 || EntryCounts[2] != 1))
 { OutError = TEXT("Required safe entry markers or player ship are missing/ambiguous."); return false; }
 ReturnControllers.Reset(); PendingReturnControllers.Reset();
 for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
 {
  ABasePlayerController* PC = Cast<ABasePlayerController>(It->Get());
  if (!PC || !PC->IsLifeCharacterAlive()) { OutError = TEXT("All players must be alive."); return false; }
  ReturnControllers.Add(PC); PendingReturnControllers.Add(PC);
 }
 for (TActorIterator<AStorageChest> It(World); It; ++It)
  if (UStorageComponent* Storage = It->GetStorageComponent()) Storage->ReturnAllReservedCursors();
 for (const auto& Entry : ReturnControllers)
 {
  ABasePlayerController* PC = Entry.Get();
  if (!PC || !PC->CleanupLifeInteraction()) { OutError = TEXT("Cannot release an interaction/cursor."); return false; }
  if (AShip* Ship = Cast<AShip>(PC->GetPawn())) Ship->ForceDisembark();
  if (ACannon* Cannon = Cast<ACannon>(PC->GetPawn())) Cannon->ForceExit();
 }
 CampaignTarget = Target;
 if (!PrepareDevelopmentCampaign(World, OutError))
 { CampaignPreparedRoom = nullptr; CampaignTarget = NAME_None; ReturnControllers.Reset(); PendingReturnControllers.Reset(); return false; }
 RecordTransitionParticipants(World);
 TransitionReason = ERoomTransitionReason::CampaignSkip; bReturning = true; bTravelAccepted = false;
 World->GetTimerManager().SetTimer(ReturnPresentationTimeoutHandle, this,
  &UClassFeatureRoomProgressSubsystem::HandleReturnPresentationTimeout, 10.f, false);
 for (const auto& PC : ReturnControllers) if (PC.IsValid()) PC->ClientBeginRoomReturn();
 return true;
#else
 return false;
#endif
}

bool UClassFeatureRoomProgressSubsystem::CommitDevelopmentCampaign(UWorld* World, FString& OutError)
{
 USWRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>();
 AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
 OutError = TEXT("The session changed while preparing campaign travel.");
 if (!Room || !Mode || Mode->GetSessionLifePhase() != ESWSessionLifePhase::Playing
  || !Room->IsDevelopmentTestSessionEnabled(World)) return false;
 // Reconcile cursors again: the client may have acted while its loading UI was opening.
 for (TActorIterator<AStorageChest> It(World); It; ++It)
  if (UStorageComponent* Storage = It->GetStorageComponent()) Storage->ReturnAllReservedCursors();
 for (const auto& PC : ReturnControllers)
  if (!PC.IsValid() || !PC->CleanupLifeInteraction())
  { OutError = TEXT("An interaction changed during loading preparation."); return false; }
 if (!PrepareDevelopmentCampaign(World, OutError)) return false;
 USWRoomSaveGame* Save = Room->GetMutableActiveRoom();
 CampaignRollbackRoom = DuplicateObject<USWRoomSaveGame>(Save, this);
 Save->HostProgress = CampaignPreparedRoom->HostProgress; Save->Guests = CampaignPreparedRoom->Guests;
 Save->SharedProgress = CampaignPreparedRoom->SharedProgress;
 Save->bFinalDepartureCompleted = CampaignPreparedRoom->bFinalDepartureCompleted;
 CampaignOptionMask = Room->DevelopmentOptionMask;
 bCampaignRestorePending = true;
 if (UStoryFacadeSubsystem::GetDevelopmentCheckpointIndex(CampaignTarget) == 4)
  Room->SetDevelopmentFinalDeparturePending(World, true, true);
 bTravelAccepted = UStoryFacadeSubsystem::GetDevelopmentCheckpointIndex(CampaignTarget) == 4
  ? Mode->RequestHostedRoomFinalDepartureTravel() : Mode->RequestHostedRoomReturnTravel();
 if (!bTravelAccepted) { OutError = TEXT("Server restart was rejected; campaign rewards were not applied."); return false; }
 return true;
}

void UClassFeatureRoomProgressSubsystem::CancelDevelopmentCampaign()
{
 if (USWRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>())
 {
  if (USWRoomSaveGame* Save = Room->GetMutableActiveRoom(); Save && CampaignRollbackRoom)
  {
   Save->HostProgress = CampaignRollbackRoom->HostProgress; Save->Guests = CampaignRollbackRoom->Guests;
   Save->SharedProgress = CampaignRollbackRoom->SharedProgress;
   Save->bFinalDepartureCompleted = CampaignRollbackRoom->bFinalDepartureCompleted;
  }
  Room->ExpectedTransitionPlayers.Reset();
  Room->SetDevelopmentFinalDeparturePending(GetGameInstance()->GetWorld(), false);
 }
 for (const auto& PC : ReturnControllers) if (PC.IsValid()) PC->ClientMessage(TEXT("Campaign skip cancelled. No rewards or story changes applied."));
 CampaignPreparedRoom = nullptr; CampaignRollbackRoom = nullptr; CampaignTarget = NAME_None;
 bCampaignRestorePending = false;
}
