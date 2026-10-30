# Item.Id 아이템 능력 정적 조사 및 GAS 구현 계획

조사일: 2026-10-14 / 대상: 현재 작업 폴더 / 구현 변경 없이 조사 및 계획만 작성

## 1. 결론과 판정 범위

네이티브 등록 태그의 말단 Item.Id 식별자는 63개다. 소모품 8개, 무기 20개, 스킬 4개, 재료 27개, 단서 4개로 분류된다. 태그 등록 수는 실제 사용 가능한 에셋 수와 다르다.

우선 구현 대상은 소모품 8종이다. 전용 C++ 사용 능력과 효과 데이터 연결을 찾지 못했고, DA_ItemData의 정적 참조 문자열에서도 소모품 GA 연결 근거가 발견되지 않았다. 무기 16종은 신규 공격 알고리즘보다 WeaponDefinition 연결과 전투 데이터 저작이 필요한 후보다.

판정 수준:
- 확정: 소스에 존재하는 실행 경로, 네이티브 태그, 실제 파일 존재, 마이그레이션 스크립트의 대상.
- 강한 정황: 소모품 8종 효과 연결 누락, 무기 16종 개별 정의 누락. 바이너리의 참조 문자열과 파일 목록을 보조 근거로 사용했다.
- 미확인: Unreal로 역직렬화한 개별 ItemDefinitions 값, Blueprint 그래프의 실제 실행 결과, 런타임 에셋 덮어쓰기. 따라서 24종을 런타임에서 모두 실패하는 확정 목록으로 해석하지 않는다.

에디터/커맨드렛/빌드/자동화 테스트는 실행하지 않았다. GE_Instant_Heal.uasset 파일은 존재하지만, 존재만으로 소모품과 연결됐다고 볼 수 없다.

## 2. 소모품 8종: 효과 구현 및 연결 우선 대상

아래 효과는 기존 기획에서 확인한 확정 사양이 아니라, 현재 Attribute와 상태 효과를 재사용하는 제안이다. 수치·지속 시간·재사용 시간은 별도 밸런스 데이터로 확정한다.

| 정확한 Item.Id 태그 | 표시명 | 조사 결과 | GAS 구현 제안 |
| --- | --- | --- | --- |
| Item.Id.Consumables.Heal.Medicine | 상처약 | 태그/기본 표시 데이터/제작 경로 존재, 전용 효과 연결 미확인 | Instant GE로 Healing 적용 |
| Item.Id.Consumables.Heal.Tangyak | 탕약 | 위와 동일 | Duration + Period GE로 지속 회복 |
| Item.Id.Consumables.Heal.Elixir | 영약 | 위와 동일 | 상처약과 동일한 Instant GE, 상위 회복량 |
| Item.Id.Consumables.Heal.Panacea | 만병통치약 | T4 태그, 초안에 effect placeholder 명시 | 큰 즉시 회복 + 허용된 해로운 효과 정화 |
| Item.Id.Consumables.Buff.Doraji | 도라지즙 | 태그/기본 표시 데이터 존재, 전용 효과 연결 미확인 | Duration GE로 MoveSpeedMultiplier 증가 |
| Item.Id.Consumables.Buff.Chungshimhwan | 청심환 | 위와 동일 | 해로운 효과 정화 또는 짧은 상태 면역; 정화부터 구현 권장 |
| Item.Id.Consumables.Buff.Gongjindan | 공진단 | 위와 동일 | Duration GE로 Strength 증가 |
| Item.Id.Consumables.Buff.RoyalGongjindan | 황실 공진단 | T4 태그, 초안에 effect placeholder 명시 | 상위 Strength 버프 + 선택적으로 AttackSpeedMultiplier 증가 |

청심환의 스턴 정화를 허용하려면 공통 행동 차단 중 사용 가능 여부를 별도로 설계해야 한다. 기본안은 행동 가능한 상태에서 독/화상 등만 정화한다. 기존 스턴·대화·사망 차단을 통째로 무시하지 않는다.

근거:
- Source/ArtisticSWCore/Private/BaseGameplayTags.cpp:219 부근: 소모품 8종 등록.
- Source/ArtisticSWCore/Public/Item/ItemData.h: FItemDefinition에 GrantedAbilityClass는 있지만 회복량/지속 시간/효과 묶음을 위한 소모품 전용 정의는 없다.
- DataTable/ItemFeatureDataTable.csv:18-23: 기본 소모품 6종 표시 행. 이 CSV에는 T4 2종이 없으므로 전체 런타임 목록으로 쓰면 안 된다.
- Documentation/BalanceDraft/item-definitions.csv:22-29: 8종과 T4 placeholder 명시. 이 파일은 초안이며 런타임 원본으로 간주하지 않는다.
- Source/ClassFeatureEditor/Private/Tests/ChestRewardAssetAuthoringTests.cpp:216 부근: T4 정의가 없으면 Elixir/Gongjindan 정의를 복사한다. T4 고유 효과를 만드는 코드는 아니다.
- Config/DefaultGame.ini:21 부근: 실제 레지스트리는 /Game/Blueprints/Item/DA_ItemData.

## 3. 무기: 구현된 공격 체계와 미완성 데이터 구분

검/활의 공격 GA와 Strength 기반 피해 계산은 구현되어 있다. 하지만 마이그레이션 스크립트는 아래 4개만 대상으로 하며, 현재 Definitions 폴더의 WD/WAS/WCD 에셋도 이 4개에 대응한다.

연결 근거가 있는 4종:
- Item.Id.Weapon.Sword.SwordA1
- Item.Id.Weapon.Sword.SwordA2
- Item.Id.Weapon.Bow.ShortBow1
- Item.Id.Weapon.Bow.LongBow1

개별 WeaponDefinition 저작/연결이 필요한 후보 16종:
- Item.Id.Weapon.Sword.SwordA3, SwordA4, SwordA5: 3종. 뒤의 축약명도 같은 접두사다.
- Item.Id.Weapon.Sword.SwordB1, SwordB2, SwordB3, SwordB4, SwordB5: 5종.
- Item.Id.Weapon.Bow.ShortBow2, ShortBow3, ShortBow4, ShortBow5: 4종.
- Item.Id.Weapon.Bow.LongBow2, LongBow3, LongBow4, LongBow5: 4종.

근거와 주의:
- Scripts/Python/migrate_weapon_definitions.py:1-17은 기존 저작된 4종만 이전한다고 명시한다.
- DA_ItemData의 참조 문자열에서도 WD_SwordA1, WD_SwordA2, WD_ShortBow1, WD_LongBow1만 발견했다. 서로 다른 아이템이 이 4개를 공유하는지는 역직렬화 없이 확정하지 않았다.
- Source/ArtisticSWCore/Private/Item/ItemSubsystem.cpp:122 부근: Weapon으로 분류된 항목의 WeaponDefinition/ActorClass가 없으면 SpawnItem이 실패한다.
- Source/ClassFeature/Private/Equipment/PlayerEquipmentComponent.cpp:564 부근: AbilitySet, 애니메이션, CombatData, 역할, 발사체 등을 검증한다.
- 같은 파일:616 부근: 무기 능력은 WeaponAbilitySet에서 부여한다. 무기에 옛 GrantedAbilityClass만 채우는 방식은 해결책이 아니다.
- SwordA2가 BP_BaseSwordB/WDA_SwordB를 사용하는 기존 매핑이 있다. 이를 SwordB1로 자동 변경하지 말고 무기 이름/전투 스타일 의도를 먼저 확정한다.

구현 방향: 기존 UGA_PlayerBasicAttack / UGA_BowAimFire를 재사용하고 각 Item.Id를 UEquippableWeaponDefinition에 연결한다. ActorClass, AbilitySet, AnimationData, CombatData, AllowedRoleTags를 채운다. 티어별 StrengthBonus/AttackCoefficient를 정의하고 몽타주·소켓·활 발사체를 검증한다. 장비 Strength는 기존 EquipmentStatComponent가 소유하도록 유지한다. 현재 장착 검증은 EquipEffects에서 Strength 수정 GE를 거부하므로 우회 중복 적용하지 않는다.

## 4. 신규 능력 누락으로 세지 않은 항목

| 구분 | 수 | 판정 |
| --- | ---: | --- |
| 스킬 | 4 | GravityVortex, WaterBomb, Bombardment, AreaSlow는 전용 C++ GA와 플레이어 기본 클래스 연결이 존재. 미구현으로 분류하지 않음. 데이터 설정 및 런타임 성공은 별도 검증 필요 |
| 재료 | 27 | 제작·강화·스킬 사용 비용 등으로 소비하는 항목. 각 재료에 직접 사용 GA를 만드는 것은 기본 요구가 아님 |
| 단서 | 4 | Clue1~4는 스토리/인벤토리 연결 대상. Item.Id 단서와 스토리 진행의 정확한 연결은 미확인. GAS 전투 능력 누락과 구분 |

추가 데이터 정리 후보:
- OxGallStone, GrapplingHook, LuminousPearl, EpicMaterial, LegendaryMaterial는 BalanceDraft에서 future special로 표기된다. 사용처 기획과 제작/강화 연결을 검토한다.
- Gunpowder는 네이티브 태그가 있지만 이번 DA_ItemData 문자열 조사에서는 발견되지 않았다. 등록과 사용처 점검 대상이다.
- AreaSlow는 네이티브 태그/PlayerSkillComponent/전용 GA에 존재하지만, 기본 아이템 CSV 및 DA_ItemData 문자열에는 보이지 않는다. 스킬 전용 경로가 있어 능력 부재는 아니며 UI/제작/레지스트리 일관성만 추가 점검한다.
- Item.Tool.Grenade/Trap/ClusterGranade/SniperRifle은 .Id 범위 밖의 레거시 아이템이므로 이번 미구현 목록에서 제외한다.

## 5. 먼저 고쳐야 할 사용 처리 계약

현재 경로는 3/4/5로 소모품 선택 → F → ActivateQuickSlot → ConsumeInventoryItem → GiveAbilityAndActivateOnce → RemoveMaterial이다.

Source/ClassFeature/Private/BasePlayer.cpp:1912 부근에서 확인한 문제:
1. 활성화 핸들이 유효하면 곧바로 수량을 차감한다. 실제 회복/버프 성공이나 이후 취소 여부를 확인하지 않는다.
2. GiveAbilityAndActivateOnce 전에 ConsumeInventoryItem 자체의 수량 검증이 없다. 퀵슬롯 호출부는 검사하지만 다른 호출 경로까지 보호하는 계약은 아니다.
3. SourceObject는 플레이어이며, ItemTag/개별 소모품 효과 정의를 GA에 전달하지 않는다. 공통 GA를 쓰려면 명시적 사용 문맥이 필요하다.
4. ItemQuickSlotTests.cpp의 사용 테스트는 UBaseGameplayAbility를 주입해 수량/입력 라우팅만 검사한다. 회복/버프 구현을 증명하는 테스트가 아니다.

제안 계약:
- ConsumeInventoryItem은 서버 검증과 GA 요청만 담당하고 수량 차감을 제거한다. 차감 책임은 소모품 GA의 성공 처리 한 곳에 둔다.
- 서버가 레지스트리에서 ItemTag를 확인하고 효과 정의를 선택한다. 클라이언트가 회복량·지속 시간·효과 클래스를 지정하게 하지 않는다.
- 초기 버전은 ServerOnly 실행을 명시한다. UBaseGameplayAbility의 기본 LocalPredicted 설정을 그대로 상속하지 않는다.
- 사용 시 서버에서 생존/행동 가능 상태, 인벤토리 잔량, 정의 유효성, 공용 재사용 시간, 버프 중복 규칙을 재검사한다.
- GA가 ItemTag와 효과 정의를 고정해 보관한다. 발동 중 퀵슬롯 선택을 다시 읽어 다른 아이템으로 바뀌지 않게 한다. 이벤트 문맥 또는 GC가 보장된 정의 SourceObject를 사용한다.
- 1차 버전은 시전 대기 없는 즉시 사용으로 범위를 줄인다. 재진입을 막고 아이템 1개를 예약한 뒤 GE를 적용한다. 실패하면 예약을 해제하고, 성공하면 정확히 1개 차감과 쿨다운을 확정한다. 기존 인벤토리 거래 함수는 참고하되 GAS 효과와 자동 원자성을 제공한다고 가정하지 않는다.
- Instant GE는 지속 효과처럼 활성 핸들 IsValid만으로 성공을 판정하지 않는다. 엔진의 적용 결과/실행 완료 신호와 효과별 성공 조건을 명시한다.
- 여러 GE를 적용하는 복합 효과는 부분 성공 규칙을 정의한다. 회복 또는 정화 중 하나라도 유효하면 소비하는 정책을 권장하며, 둘 다 불가능하면 거부한다. 이미 실행된 Instant 회복을 단순 핸들 제거로 되돌리려 하지 않는다.
- 추후 시전 애니메이션을 추가하면 성공 시점까지 예약을 유지하고 피격/사망/취소 시 해제한다.

## 6. GAS 구성과 재사용

### 공통 능력과 데이터

- ClassFeature: UGA_UseConsumable을 새로 만들고 기존 BaseGameplayAbility, BasePlayer, InventoryComponent와 통합한다. 아이템마다 C++ GA를 8개 만들 필요는 없다.
- ArtisticSWCore: FItemDefinition에 소모품 효과 정의 참조를 추가하고 읽기 전용 UConsumableDefinition 데이터 에셋을 제안한다. ItemTag, 효과 유형, GE 클래스, 회복량/버프량, 지속 시간, 주기, 쿨다운 계열, 중복 정책, 정화 허용 목록, Cue를 정의한다.
- GASCore: 회복/지속 회복/능력치 버프/쿨다운 GE의 공통 구현. 낮은 계층인 ArtisticSWCore에 ClassFeature 인벤토리 의존성을 추가하지 않는다.
- 처음에는 기존 GrantedAbilityClass를 소모품 공통 GA에 연결한다. 별도의 장착 액터 생성 없이 퀵슬롯에서 사용한다.

### 회복

UBaseAttributeSet의 Healing 메타 Attribute 및 Health 상한 보정, UGASCombatLibrary::MakeHealingEffectSpec의 Data.Heal SetByCaller를 재사용한다. Instant 회복 GE는 Healing을 수정해야 하며 Data.Heal을 실제 Modifier가 읽도록 정의한다. 현재 GE_Instant_Heal 에셋의 실제 속성은 에디터 점검 후 재사용 여부를 결정한다.

지속 회복은 HasDuration + Period로 구현하고 첫 틱 즉시 실행 여부와 틱 수를 명확히 한다. 사망 후에는 틱 회복이 생명주기를 우회하지 않도록 Healing 경로에도 차단을 설계한다. 현재 BaseAttributeSet의 명시적 사망 차단은 Damage 분기에 있으므로 회복이 자동으로 차단된다고 가정하지 않는다.

### 버프와 정화

Strength, MoveSpeedMultiplier, AttackSpeedMultiplier는 이미 존재하므로 우선 재사용한다. 이동과 공격 속도는 실제 소비 코드 및 최종 제한값도 함께 점검한다. 버프는 HasDuration GE로 적용하고 만료 시 원복되게 한다. 동일 계열은 1개만 허용하며 동일 등급 갱신/상위 교체/하위 거부 정책을 데이터로 정한다. 다른 GE 클래스끼리는 StackLimitCount만으로 상호 배제가 되지 않으므로 공통 계열 태그로 관리한다.

정화는 State.Status.Poison/Burn 등 명시적 허용 목록에 해당하는 활성 GE만 제거한다. 모든 상태 태그/모든 GE를 일괄 제거하지 않는다. 특히 State.Control.ActionsBlocked는 여러 효과가 공유하므로 loose tag를 강제로 지우지 않는다. 면역은 기존 StatusEffectLibrary의 적용 판정과 연계해 별도 단계로 구현한다.

### 재사용 시간, 표시, 저장

회복/버프 계열의 쿨다운 GE와 태그를 ASC에 둔다. 퀵슬롯을 바꾸거나 새 GA 인스턴스를 만들어도 쿨다운을 우회하지 못하게 한다. UI에는 사용 불가 이유·남은 시간·버프 지속 시간을 노출하고 이펙트/사운드는 GameplayCue로 분리한다.

룸 복귀/저장 경로가 USWRoomAbilitySystemComponent에서 활성 GE 클래스와 잔여 시간을 다루므로 저장 가능한 고정 GE 클래스를 사용한다. 버프/쿨다운/지속 회복의 복귀 정책과 중복 복원 여부를 검증한다. 이번 조사에서 해당 저장 흐름을 수정하지 않는다.

## 7. 구현 순서와 완료 조건

| 단계 | 작업 | 완료 조건 |
| --- | --- | --- |
| P0 | 아이템별 연결 감사 + 소모품 사용 문맥/차감 계약 | 잘못된 정의, 잔량 0, 쿨다운, 취소 시 효과/수량 변경 없음 |
| P1 | 상처약 Instant 회복을 종단 구현 | 서버 체력 증가, 최대 체력 보정, 정확히 1개 차감, 클라이언트 UI 반영 |
| P2 | 나머지 회복 3종 + 버프 4종 | 지속 회복 틱/만료, 버프 교체, 정화 범위, 사망 처리 일관성 |
| P3 | 미연결 무기 16종 데이터 저작 | 20종 모두 정의/능력/애니메이션 검증 통과, 장착·해제 스탯 정상 |
| P4 | 데이터 일관성 및 플레이 검증 | 레지스트리/표시/제작/드롭 정합성, 멀티플레이/룸 복귀 정상 |

에셋 연결 감사는 별도의 읽기 전용 에디터 검사로 정확한 ItemTag → GA/GE/WeaponDefinition 매핑을 출력하는 것이 좋다. 실행은 사용자가 명시적으로 에디터 검증을 요청한 때 수행한다. 현재 스크립트 중 마이그레이션/저작 스크립트는 데이터를 저장하므로 단순 조사 목적으로 실행하지 않는다.

추후 검증 시나리오:
- 8종 각각 정상 사용, 잔량 0, 최대 체력, 사망/대화/행동 차단, 잘못된 GE, 정화 대상 없음.
- 빠른 연속 입력, 슬롯 변경, 인벤토리 이동과 사용 경쟁: 수량 중복 차감/무료 효과/쿨다운 우회 없음.
- 실패 및 취소: 예약 해제, 수량/효과/쿨다운이 정의된 계약과 일치.
- 탕약 틱 수/총량/사망 중단, 공진단 계열 중복/교체/만료, 장비 교체 중 버프 유지.
- 호스트와 원격 클라이언트의 체력/수량/버프 표시, 사망·리스폰·룸 복귀에서 중복 효과 없음.
- 무기 20종 장착/기본 공격/투사체/역할 제한/해제 및 Strength 원복.

C++ 및 UPROPERTY/UCLASS 변경 시 선언·구현·의존성·GC·서버 권한·CRLF·diff를 정적으로 확인한다. 실제 컴파일/UHT 및 에디터 테스트는 후속 명시 요청 시 수행한다.

## 8. 참고와 이번 변경

GAS의 Instant/HasDuration/Periodic 구분은 Epic 공식 Gameplay Effects 문서의 기본 구조를 따른다. 구체 API는 구현 시 프로젝트에 설치된 UE 5.7 헤더를 기준으로 확인한다.
https://dev.epicgames.com/documentation/unreal-engine/gameplay-effects-for-the-gameplay-ability-system-in-unreal-engine

이번 작업은 이 계획 문서만 추가했다. 기존 SWRoomSubsystem.cpp/.h 수정 및 docs/Local_Room_Test_Commands.md는 사용자 작업으로 보존했다. 코드/설정/바이너리 에셋을 수정하지 않았다.
