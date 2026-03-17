// Source/GASCore/Public/GASInputID.h

#pragma once

#include "CoreMinimal.h"

/**
 * EGASInputID
 * GAS 어빌리티와 실제 키보드 입력(Enhanced Input)을 연결해주는 공통 ID입니다.
 */
UENUM(BlueprintType)
enum class EGASInputID : uint8
{
    None = 0,
    Confirm = 1,
    Cancel = 2,

    // 무기/도구 스킬 사용 (Q키)
    UseSkill = 3,

    // 상호작용 (F키 - 필요 시 사용)
    Interact = 4
};