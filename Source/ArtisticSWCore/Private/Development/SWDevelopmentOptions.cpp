#include "Development/SWDevelopmentOptions.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "Room/SWRoomReadyState.h"

int32 SWDevelopmentOptions::GetMask(const UObject* Context)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
 UWorld* World = Context ? Context->GetWorld() : nullptr;
 if (!World || !World->IsGameWorld()) return 0;
 if (World->GetNetMode() != NM_Client)
 {
  const USWRoomProgressSubsystem* Room = World->GetGameInstance()
   ? World->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
  return Room && Room->IsDevelopmentTestSessionEnabled(World) ? Session | Room->DevelopmentOptionMask : 0;
 }
 for (TActorIterator<ASWRoomReadyState> It(World); It; ++It)
  return It->bWorldReady ? It->DevelopmentOptionMask : 0;
#endif
 return 0;
}

bool SWDevelopmentOptions::IsEnabled(const UObject* Context, EOption Option)
{
 const int32 Mask = GetMask(Context);
 return (Mask & Session) != 0 && (Mask & Option) != 0;
}
