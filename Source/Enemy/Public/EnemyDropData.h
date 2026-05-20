#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "BaseGameplayTags.h"
#include "EnemyDropData.generated.h"

/*
전체 표에서 Tag로 특정 Row를 찾아 FEnemyDropDataRow에 전부 저장

FEnemyDropDataRow에 저장된 데이터에서 드랍할 아이템의 수 만큼 FEnemyDropEntry 생성하고 각 아이템 정보를 저장

FEnemyDropData에 FEnemyDropEntry정보 저장

위 과정이 초기화 단계이고, Enemy는 런타임동안 FEnemyDropData하나만 가지고 있게 함
*/

// 드랍할 아이템 하나마다 갖는 구조체
USTRUCT(BlueprintType)
struct FEnemyDropEntry
{
    GENERATED_BODY()

    // 아이템 태그
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag ItemTag;

    // 드랍 확률
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DropChance = 0.f;
};

// 특정 적 하나가 가지는 드랍 정보 (런타임)
USTRUCT(BlueprintType)
struct FEnemyDropData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FGameplayTag EnemyTag;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 DropItemCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TArray<FEnemyDropEntry> DropEntries;
};

// 데이터 테이블의 한 Row 전체를 가져오기 위한 구조체 (CSV import)
USTRUCT(BlueprintType)
struct FEnemyDropDataRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag EnemyTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 DropItemCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag ItemTag_1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DropChance_1 = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag ItemTag_2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DropChance_2 = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag ItemTag_3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DropChance_3 = 0.f;
};