#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WaveSystem/Data/WaveSpawnTypes.h"
#include "WaveDataAsset.generated.h"

/**
 * Wave Spawn System의 정적 데이터 에셋.
 *
 * 책임:
 * - 전체 WaveDefinition 배열 보관
 * - WaveArrayIndex 기준 조회
 * - DisplayWaveNumber 기준 조회
 * - 데이터 검증
 *
 * 멀티플레이 기준:
 * - 런타임 상태를 넣지 않는다.
 * - AliveEnemyCount, RemainingCount 같은 실행 상태를 넣지 않는다.
 * - 서버와 클라이언트가 같은 Asset을 읽을 수는 있지만, Wave 실행 결정은 서버가 한다.
 */
UCLASS(BlueprintType)
class ENEMY_API UWaveDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    /**
     * 전체 Wave 목록.
     *
     * 내부 실행 기준:
     * - WaveArrayIndex 0, 1, 2 ...
     *
     * UI 표시 기준:
     * - FWaveDefinition::DisplayWaveNumber
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave Data")
    TArray<FWaveDefinition> WaveDefinitions;

public:
    /**
     * Wave 총 개수 반환.
     */
    UFUNCTION(BlueprintPure, Category = "Wave Data")
    int32 GetWaveCount() const;

    /**
     * 배열 인덱스가 유효한지 확인.
     */
    UFUNCTION(BlueprintPure, Category = "Wave Data")
    bool IsValidWaveIndex(int32 WaveArrayIndex) const;

    /**
     * 배열 인덱스로 WaveDefinition 조회.
     *
     * C++ 전용 Checked 함수.
     * Blueprint에서는 안전한 GetWaveDefinition을 사용한다.
     */
    const FWaveDefinition& GetWaveDefinitionChecked(int32 WaveArrayIndex) const;

    /**
     * Blueprint용 안전 조회 함수.
     *
     * 반환값:
     * - true: 조회 성공
     * - false: 인덱스 invalid
     */
    UFUNCTION(BlueprintPure, Category = "Wave Data")
    bool GetWaveDefinition(int32 WaveArrayIndex, FWaveDefinition& OutWaveDefinition) const;

    /**
     * DisplayWaveNumber로 WaveArrayIndex 찾기.
     *
     * 예:
     * - DisplayWaveNumber 1 -> 배열 인덱스 0
     */
    UFUNCTION(BlueprintPure, Category = "Wave Data")
    bool FindWaveIndexByDisplayNumber(int32 DisplayWaveNumber, int32& OutWaveArrayIndex) const;

    /**
     * DisplayWaveNumber로 WaveDefinition 찾기.
     */
    UFUNCTION(BlueprintPure, Category = "Wave Data")
    bool GetWaveDefinitionByDisplayNumber(int32 DisplayWaveNumber, FWaveDefinition& OutWaveDefinition) const;

    /**
     * WaveData 전체 검증.
     *
     * 이 함수는 에디터와 런타임 양쪽에서 호출 가능하게 만든다.
     * AWaveSpawnManager::ValidateWaveData에서 이 함수를 재사용한다.
     */
    UFUNCTION(BlueprintCallable, Category = "Wave Data")
    bool ValidateWaveData(TArray<FString>& OutErrors) const;

    /**
     * 검증 결과를 로그로 출력.
     *
     * 에디터에서 DataAsset 세팅 후 빠르게 확인하기 위한 편의 함수.
     */
    UFUNCTION(BlueprintCallable, Category = "Wave Data")
    bool ValidateWaveDataAndLog() const;

private:
    void AddValidationError(TArray<FString>& OutErrors, const FString& Error) const;
};