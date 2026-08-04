# Strength 기반 공통 대미지 MVP 설정 및 검증 가이드

## 구현된 런타임 기본값

- `UBaseAttributeSet::Strength` 기본값은 10이며 서버에서 클라이언트로 RepNotify 복제됩니다.
- 장착 Strength 효과의 네이티브 기본 클래스는 `UGASStrengthEquipmentGameplayEffect`입니다.
  - Infinite
  - `Data.StrengthBonus` SetByCaller
  - `Strength` Additive
- 직접 피해의 네이티브 기본 클래스는 `UGASDamageInstantGameplayEffect`입니다.
  - Instant
  - `Data.Damage` SetByCaller
  - `UBaseAttributeSet::Damage` Meta Attribute만 수정
- 직접 피해 공식은 다음과 같습니다.

  `max(1, Strength * AttackCoefficient * ChargeMultiplier)`

## 1. Attribute 초기화 GE

캐릭터에 이미 적용 중인 초기 Attribute GE가 있다면 Player와 Enemy용 GE 모두에 다음 Modifier를 추가합니다.

1. Attribute: `BaseAttributeSet.Strength`
2. Operation: `Override`
3. Magnitude: `10`

초기화 GE가 아직 없는 캐릭터도 C++ 기본값 10으로 동작하지만, 캐릭터별 초기 스탯을 데이터로 관리하려면 초기화 GE에서 명시하는 것을 권장합니다. `AttackPower`는 호환성을 위해 남아 있으나 신규 검/화살 공격 계산에서는 사용하지 않습니다.

## 2. 무기 Strength 설정

검과 활 BP의 Class Defaults에서 `Item > Strength > Strength Bonus`를 설정합니다.

- 기본 검/활 예시: `5`
- 장착 시 Strength: `10 -> 15`
- 해제/교체/파괴 시: `15 -> 10`

`PlayerEquipmentComponent > Equipment > Strength > Strength Equipment Effect Class`는 기본적으로 네이티브 공통 GE를 사용합니다. 별도 에셋이 필요하면 `UGASStrengthEquipmentGameplayEffect`를 부모로 BP GE를 만들고 이 항목에 지정합니다. 상속된 Strength Modifier를 중복해서 추가하지 않습니다.

## 3. 검 BP 설정

`ASwordItem` 기반 검 BP에서 다음 값을 설정합니다.

1. `Sword > Damage > Attack Coefficient`
2. 필요 시 `Damage Effect Class`
   - 비어 있으면 네이티브 `UGASDamageInstantGameplayEffect` 사용
   - 에셋이 필요하면 해당 네이티브 클래스를 부모로 `GE_Damage_Instant` 생성
3. `Sword > Status > Status Effect Classes`
   - 일반 검: 빈 배열
   - 불검: 화상 Duration/Periodic GE 추가

기존 `Base Damage`, `Attack Power Multiplier`, `CalculateDamage()`는 호환성을 위해 남아 있지만 Strength 기반 신규 공격에서는 사용하지 않습니다. 한 공격 윈도우에서는 같은 Actor가 한 번만 처리되며 직접 피해 적용 후 배열 순서대로 상태이상 GE가 적용됩니다.

## 4. Projectile BP 설정

공통 로직은 `AArrowProjectile`에 있습니다.

- Player 화살 BP 부모: `APlayerArrowProjectile`
- Enemy 화살 BP 부모: `ARangedEnemyProjectile`
- 두 클래스 모두 `AArrowProjectile`을 상속합니다.

각 Projectile BP의 `Arrow > Damage > Damage Data`에서 다음을 설정합니다.

1. `Direct Damage Effect Class`
   - 비워 두면 공통 네이티브 즉시 피해 GE 사용
2. `Attack Coefficient`
3. `Direct Damage Effect Level`
4. `Status Effects`
   - 화상/중독 등 여러 개 설정 가능
   - 각 항목의 Effect Class, Effect Level, 필요 시 Refresh Granted Tag 설정
5. 관통형이면 기존 충돌/파괴 옵션에 맞게 `Destroy On Impact`를 끄고 관통 동작을 구성

기존 `Damage Effects`, Crit 관련 필드는 이전 BP 직렬화 호환용입니다. 직접 피해 클래스는 구형 배열의 첫 유효 클래스를 임시 fallback으로 읽지만, 피해량은 항상 Strength 공식으로 계산됩니다. MVP에서 Crit 계산은 하지 않습니다.

Projectile은 발사 GA가 만든 최종 Damage Spec을 발사 시점에 보관합니다. 따라서 화살 비행 중 무기를 해제하거나 Strength가 바뀌어도 이미 발사된 화살의 피해는 변하지 않습니다. 상태이상 Spec도 발사 시 Source ASC 문맥을 포함한 상태로 생성됩니다.

## 5. 상태이상 GE 주의사항

- 상태이상 GE 자체의 Duration, Period, Modifier를 에셋에 설정합니다.
- 동일한 상태이상 GE 클래스가 이미 적용 중이면 기존 ActiveEffect를 제거한 뒤 새 Spec을 적용합니다. 따라서 중첩 수는 항상 1이고 Duration/Period 타이머가 재적중 시점부터 다시 시작합니다.
- Projectile의 `Refresh Granted Tag`는 선택 사항입니다. 지정하면 서로 다른 GE 클래스라도 같은 Granted Tag를 사용하는 상태이상 그룹의 기존 효과를 제거합니다.
- 검의 MVP 배열은 단순 순차 적용이며 복잡한 스택/확률 처리는 포함하지 않습니다.
- 화상/중독 GE에서 Source/Instigator가 필요하면 Effect Context의 Original Instigator를 사용합니다.

## 6. PIE 수동 검증

네트워크 검증은 PIE `2 Players`, `Play As Client` 또는 Dedicated Server 옵션으로 수행합니다.

1. 서버와 클라이언트에서 초기 Strength가 모두 10인지 확인
2. Strength Bonus 5 무기 장착 후 양쪽 모두 15인지 확인
3. 같은 장착 요청을 반복해도 20으로 증가하지 않는지 확인
4. 해제, 교체, 무기 Actor 파괴 각각에서 10으로 복구되는지 확인
5. AttackCoefficient 1.5에서 Strength 10이면 직접 피해가 15인지 확인
6. 일반 검은 직접 피해만, 불검은 직접 피해 후 화상을 적용하는지 확인
7. 화상+중독 Projectile이 두 상태이상을 모두 적용하는지 확인
8. 화살 발사 후 무기를 해제/교체해도 해당 화살 피해가 유지되는지 확인
9. 관통 Projectile이 같은 대상을 다시 겹쳐도 한 번만 적용하는지 확인
10. 클라이언트 충돌만으로 피해가 중복 적용되지 않고 서버 권한 결과만 복제되는지 확인

## 7. 자동화 테스트

다음 필터를 Session Frontend 또는 명령줄에서 실행합니다.

- `ArtisticSW.GAS.Strength`
  - 기본 Strength와 공식/최소 피해
  - 발사 시점 Damage Spec 스냅샷
  - Damage Meta Attribute 단일 입력
  - 장착 +5, 중복 장착 방지, 해제 원복
  - 근접 직접 피해 후 복수 상태이상, 대상별 1회, Source 문맥
- `ArtisticSW.Enemy.RangedEnemy`
  - 기존 RangedEnemy 회귀
  - Player/Enemy 공통 Arrow 부모
  - 진영 필터 옵션
  - Projectile 직접 피해 후 복수 상태이상
  - 관통 대상별 1회와 Source 문맥

## 8. 유지할 경고 로그

MVP 관련 상세 추적 로그는 제거했습니다. 런타임에서 다음 실패 상황만 경고로 남습니다.

- SourceASC 없음
- DamageEffectClass 없음
- TargetASC 없음
- 유효하지 않은 Damage Spec
- 장착 GE Handle 제거 실패
