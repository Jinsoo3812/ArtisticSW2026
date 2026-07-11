#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "BaseGameplayTags.h"
#include "EnemyDropData.generated.h"

// 적 Storage에 들어갈 아이템 하나의 생성 규칙
USTRUCT(BlueprintType)
struct FEnemyDropEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop")
    FGameplayTag ItemTag;

    // 활성화하면 확률 판정 없이 항상 Storage에 들어간다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop")
    bool bGuaranteed = false;

    // 보장 아이템이 아닐 때 독립적으로 판정하는 확률 (0.0 ~ 1.0)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "!bGuaranteed"))
    float DropChance = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop", meta = (ClampMin = "1", UIMin = "1"))
    int32 MinCount = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop", meta = (ClampMin = "1", UIMin = "1"))
    int32 MaxCount = 1;
};

// 특정 적 하나가 생성할 Storage의 아이템 규칙
USTRUCT(BlueprintType)
struct FEnemyDropData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FGameplayTag EnemyTag;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TArray<FEnemyDropEntry> DropEntries;
};

// EnemyDropTable의 Row. EnemyTag별로 원하는 만큼 아이템 규칙을 추가할 수 있다.
USTRUCT(BlueprintType)
struct FEnemyDropDataRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop")
    FGameplayTag EnemyTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop")
    TArray<FEnemyDropEntry> DropEntries;
};
