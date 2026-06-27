// Source/GASCore/Public/GASInputID.h

#pragma once

#include "CoreMinimal.h"

/**
 * GameplayAbility와 입력을 연결할 때 사용하는 기본 입력 ID입니다.
 * 현재 프로젝트는 GameplayTag 기반 입력을 주로 사용하지만, Confirm/Cancel처럼
 * GAS 기본 입력 ID가 필요한 경우를 위해 유지합니다.
 */
UENUM(BlueprintType)
enum class EGASInputID : uint8
{
	// 입력이 지정되지 않은 상태입니다.
	None = 0,

	// GAS Targeting confirm 입력입니다.
	Confirm = 1,

	// GAS Targeting cancel 입력입니다.
	Cancel = 2,

	// 기본 스킬 입력입니다. 필요 시 Enhanced Input 액션과 매핑합니다.
	UseSkill = 3,

	// 상호작용 입력입니다. 필요 시 F키 계열 액션과 매핑합니다.
	Interact = 4
};
