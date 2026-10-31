# 소모품 GAS 구현 및 무기 연결 적용 안내

작성일: 2026-10-30

## 반영한 범위

소모품 8종은 기존 Item.Id와 아이템 레지스트리를 통해 공통 UGA_UseConsumable로 사용한다. 레지스트리의 CategoryTag가 Item.Category.Consumable이고 해당 아이템을 보유해야 한다. 별도의 소모품 BP/GE 에셋을 새로 만들 필요 없이 네이티브 GA/GE 기본값으로 작동하도록 연결했다. 실제 컴파일 및 플레이 확인은 아직 수행하지 않았다.

| 아이템 | 기본 효과 | 지속/쿨다운 |
| --- | --- | --- |
| Medicine / 상처약 | 체력 25 회복 | 회복 계열 쿨다운 5초 |
| Tangyak / 탕약 | 초당 체력 10 회복, 기본 5틱 | 지속 5.1초, 회복 쿨다운 5초 |
| Elixir / 영약 | 체력 60 회복 | 회복 쿨다운 5초 |
| Panacea / 만병통치약 | 체력 100 회복 및 독/화상 정화 | 회복 쿨다운 5초 |
| Doraji / 도라지즙 | MoveSpeedMultiplier × 1.2 | 20초, 보조 계열 쿨다운 5초 |
| Chungshimhwan / 청심환 | 독/화상 정화 | 보조 쿨다운 5초 |
| Gongjindan / 공진단 | Strength +5 | 30초, 보조 쿨다운 5초 |
| RoyalGongjindan / 황실 공진단 | Strength +10, AttackSpeedMultiplier × 1.15 | 30초, 보조 쿨다운 5초 |

위 수치는 구현용 초깃값이며 최종 밸런스가 아니다. 탕약은 적용 즉시 틱을 실행하지 않고 1초부터 회복한다. 종료 시점과 마지막 틱이 겹치지 않도록 기본 지속 시간을 5.1초로 설정했다. 지연 프레임·저장 복귀에서 틱 타이밍은 실제 플레이 검증이 필요하다.

## 데이터 수정 방법

DA_ItemData의 각 ItemDefinitions 항목에 Consumable 구조체가 추가됐다.

- Effect = NativeDefault: 위 8종의 기본값을 태그로 결정한다. 이 모드에서는 구조체의 나머지 수치 수정이 적용되지 않는다.
- 수치를 직접 조정하려면 Effect를 InstantHeal, Regeneration, Cleanse, HealAndCleanse, MovementBuff, StrengthBuff 중 하나로 바꾸고 필요한 값을 설정한다.
- Effect = Disabled: 해당 소모품 사용을 막는다.
- Healing은 즉시 회복량 또는 1초 틱당 회복량이다.
- Rank는 버프 우선순위다. 같은 등급은 갱신, 높은 등급은 교체, 낮은 등급은 거부한다. 최대 4이다.
- NativeDefault가 아닌 커스텀 아이템도 유효한 Consumable 정의와 CategoryTag를 지정하면 공통 능력을 사용한다.
- 이 경로는 GrantedAbilityClass를 실행하지 않는다. 임의의 GA 활성화만으로 아이템이 차감되는 이전 동작을 제거했다. 레거시 장착 도구의 능력 부여 경로는 그대로다.

## 사용과 수량 처리

입력은 기존 3/4/5 선택 → F 사용을 유지한다. 서버가 아이템·상태·수량·쿨다운을 검증하고, 요청마다 ItemTag를 고정한다. 슬롯 변경으로 사용 대상이 바뀌지 않는다.

TryConsumeItemWithEffect는 제거 예정 인벤토리 사본을 만들고, 동기 효과 적용 동안 인벤토리 변경의 재진입을 차단한다. 효과가 성공한 경우에만 사본을 확정하고 변경 이벤트를 한 번 보낸다. 실패 시 수량과 퀵슬롯 선택은 유지된다. 이 API의 콜백은 동기 실행만 허용하며, 실패 반환 전에 자신의 효과 부작용을 정리해야 한다.

쿨다운 GE를 먼저 적용하고 실제 효과가 실패하면 쿨다운을 제거한다. 즉시 회복은 활성 GE 핸들이 아니라 BaseAttributeSet.OnHealingResolved의 동일 실행 문맥과 실제 회복량으로 성공을 확인한다. 회복 직후 다른 콜백이 피해를 주어도 이미 수행한 회복은 정상 소비로 처리한다.

만병통치약은 회복/정화 중 하나라도 성공하면 1개 소비한다. 둘 다 필요 없으면 소비하지 않는다. 순수 회복은 최대 체력에서 거부하며, 청심환은 정화할 활성 독/화상 GE가 없으면 거부한다. 스턴·대화·행동 차단을 무시하는 해제 능력은 아니다.

지속 회복은 중복 사용을 거부한다. 공진단 두 종류는 같은 GE 클래스를 1스택으로 갱신하므로 일시적인 이중 Strength 보너스가 생기지 않는다. 버프 및 지속 회복은 State.Dead에서 제거하며, Healing 메타 Attribute도 사망/체력 0/비정상 수치에서는 실행하지 않는다. 부활은 기존 생명주기 경로에서 별도로 처리해야 한다.

## UI 및 저장

- BlueprintPure ABasePlayer.GetConsumableCooldownRemaining(ItemTag)로 남은 계열 쿨다운을 조회할 수 있다.
- 회복 계열 태그: Cooldown.Item.Recovery.
- 보조 계열 태그: Cooldown.Item.Utility.
- 지속 효과 태그: State.Buff.Consumable.Regeneration / Movement / Strength.
- 네이티브 GE 클래스와 고정 부여 태그, SetByCaller 수치, 레벨을 사용하므로 기존 SWRoomAbilitySystemComponent의 저장 형식과 맞춘다. 룸 복귀 동작은 실행 검증하지 않았다.
- HUD 쿨다운 텍스트/게이지의 실제 배치와 GameplayCue 시각·음향 에셋은 이번에 저작하지 않았다.

## 무기 16종

Scripts/Python/complete_item_weapon_definitions.py에 미연결 무기 정의 생성·연결 작업을 준비했다. 에디터 실행 정책에 따라 스크립트는 실행하지 않았으며 바이너리 에셋은 아직 변경되지 않았다.

이 스크립트는 기존 4종의 WeaponAbilitySet, 액터, 애니메이션, 투사체를 재사용하고 누락된 WD/WCD를 생성한다. 모든 대상 행과 원본 참조, 기본 공격/조준 몽타주를 먼저 검증한다. 기존 WeaponDefinition 연결이나 이미 있는 개별 에셋 값은 덮어쓰지 않는다.

대상: SwordA3~5, SwordB1~5, ShortBow2~5, LongBow2~5.

SwordB 계열은 기존 SwordA2에 연결된 B형 전투 프로필을 재사용한다. 기존 SwordA2 매핑은 보존한다. 임시 Strength 배율은 등급 1~5에 1.0/1.25/1.5/1.75/2.0이며, 원본 등급에 대해 비례 보정한다. AttackCoefficient는 원본 값이다. 원본 StrengthBonus가 0이면 결과도 0이므로 최종 무기 밸런스는 별도로 작성해야 한다.

실제 적용에는 사용자 측 C++ 컴파일/UHT 완료 후 Unreal Python 콘솔에서 이 스크립트를 실행해야 한다. 그 뒤 무기별 장착 검증, 역할 제한, 몽타주/소켓, 투사체, 해제 시 능력/스탯 회수를 확인한다. 기존 에셋의 세부 설정에 따라 추가 저작이 필요할 수 있다.

## 파일 구성

- ArtisticSWCore: Item/ConsumableDefinition.h/.cpp 신규, ItemData.h에 설정 추가.
- GASCore: Effects/ConsumableGameplayEffects.h/.cpp 신규, BaseAttributeSet.h/.cpp에 회복 완료 통지 및 사망 차단.
- ClassFeature: GAS/Ability/GA_UseConsumable.h/.cpp 신규, BasePlayer.h/.cpp 사용 경로와 쿨다운 조회, InventoryComponent.h/.cpp 동기 소비 거래.
- Scripts/Python/complete_item_weapon_definitions.py: 후속 무기 에셋 적용용.

## 검증 상태 및 후속 확인

수행: 변경 diff 검토, 선언/구현 및 모듈 의존성 확인, UE 5.7 로컬 헤더/구현과 GAS API 대조, 새 Python 스크립트의 구문 분석, 텍스트 CRLF 확인.

미수행: Unreal 빌드/UHT/에디터/자동화 테스트/PIE/네트워크·룸 복귀 검증. 새 반영 타입이 있으므로 실제 컴파일이 필요하다.

사용자가 EffectsAndTransactions 및 FUsePriority 테스트의 성공을 확인했다. 검증 완료 후 요청에 따라 이번 작업에서 추가한 테스트 코드를 제거하고 기존 테스트 파일의 변경을 되돌렸다.
