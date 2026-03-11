#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

// 프로젝트에서 사용될 모든 태그를 선언

// State (상태 관련 태그)
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Attacking);
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Damaged);


// Team  (피아 구분 관련 태그 - 타겟팅 필터링용)
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Team_Player);
GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Team_Enemy);
