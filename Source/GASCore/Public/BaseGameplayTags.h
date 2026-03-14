#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

// ?꾨줈?앺듃?먯꽌 ?ъ슜??紐⑤뱺 ?쒓렇瑜??좎뼵

// State (?곹깭 愿???쒓렇)
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Attacking);
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Damaged);

// Team  (?쇱븘 援щ텇 愿???쒓렇 - ?寃잜똿 ?꾪꽣留곸슜)
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Team_Player);
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Team_Enemy);

// GameplayAbility
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayAbility_Active);
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayAbility_Dead);
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayAbility_HitReaction);
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayAbility_TestHit);

// Event (?대깽??愿???쒓렇)
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Ability_Changed);

// Data
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Damage);
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Heal);

