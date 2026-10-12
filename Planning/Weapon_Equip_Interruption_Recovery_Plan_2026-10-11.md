# 검·활 장착 중 피격·구르기 발생 시 복구 계획

작성일: 2026-10-11. 상태: 코드·기존 로그를 확인한 설계안. 이 문서 작성으로 게임 코드와 에셋을 수정하지 않았다. 피격·구르기의 정확한 발생 프레임을 기록한 재현 검증은 구현 단계에서 수행한다.

## 1. 결론

호출 순서에 실제 결함이 있다. 특히 검에서 활로 교체하면 새 활에 연결한 발사 위치를 이전 검 정리 코드가 해제할 수 있다. 여기에 장착 애니메이션 재생 실패, 늦은 Notify, 상태 복제 순서가 겹치면 보이는 무기와 사용 가능한 무기가 달라질 수 있다.

장착은 `생성 → 부착 → 능력 부여 → 스탯 적용 → 확정`으로 관리하고, 확정 전 취소는 이전 장비로 복구한다. 확정 후 피격·구르기는 행동만 중단하며 확정된 장비의 능력 부여는 유지한다. 기존 EquipmentComponent를 장착의 책임자로 유지하고 성공·취소·실패 종료 경로를 통합하는 안을 권장한다.

## 2. 확인한 코드와 원인 후보

| 항목 | 확인한 사실 | 증상과의 관계 / 확실성 |
| --- | --- | --- |
| 새 활 연결을 이전 검이 해제 | FinalizePendingEquip에서 새 무기의 AttachItem을 먼저 실행한다. 이후 StoreCurrentEquippedItem이 이전 무기를 ABowItem으로 변환해 ClearBowArrowAnchor에 전달한다. 이전 무기가 검이면 nullptr이고, nullptr는 현재 연결된 활 전체를 해제한다. | 코드로 확인한 결함. 검→활에서 외형과 능력 Spec이 정상이어도 실제 발사가 실패할 수 있다. |
| 취소에서도 nullptr의 의미가 혼합 | CancelPendingEquip 역시 취소 대상이 검이면 ClearBowArrowAnchor(nullptr)를 호출한다. 이후 이전 장비를 다시 부착하는 복구는 있지만 그 성공을 확인하지 않는다. | 다른 장비의 연결을 건드리는 위험. 현재 취소 함수가 전혀 없는 것은 아니다. |
| 장착 중 상태가 남는 경로 | PlayEquipmentMontage는 Mesh/AnimInstance가 없으면 조용히 반환한다. Montage_Play의 성공 여부도 확인하지 않는다. | StartEquipItem이 설정한 Equipping이 종료되지 않으면 입력과 무기 능력이 계속 차단된다. 실제 발생 빈도는 재현 필요. |
| 장착 Notify와 콜백의 식별 부족 | Notify는 요청 번호나 재생 인스턴스 정보 없이 HandleEquipmentAttachNotify를 호출한다. 종료 콜백은 Montage 에셋 포인터만 비교한다. | 이전 요청의 늦은 이벤트가 같은 몽타주를 사용하는 새 요청에 적용될 가능성. |
| 확정 과정의 부분 완료 | 새 무기 부착·능력·스탯을 적용한 다음 이전 무기를 정리한다. StoreCurrentEquippedItem(false)의 반환값은 확인하지 않는다. | 중간 실패·동기 콜백 재진입 때 전체 장착의 복구 기준이 없다. Stats.Equip 내부 복구는 존재하지만 전체 장착을 복구하지는 않는다. |
| 행동 허용 기준이 서로 다름 | CanChangeEquipment는 Dead/Attacking/Bow 상태를 확인하지만 Rolling/Damaged/Stun/ActionsBlocked는 확인하지 않는다. 일반 OnAbilityInputPressed는 장착 중 입력 전체를 거절한다. Roll GA의 자체 판정에는 장착 중 검사가 없다. | 구르기 중 장착 시작, 별도 BP/직접 GA 활성화 등의 경로에 따라 결과가 달라질 수 있다. 일반 입력만으로 장착 중 구르기가 실행된다고 단정할 수 없다. |
| 복제 상태와 연출 상태가 혼합 | Multicast_PlayEquipmentMontage가 각 머신의 EquipmentState를 Equipping으로 바꾼다. EquippedItem과 EquipmentState는 별도로 복제되며 OnRep_EquipmentState는 비어 있다. | 클라이언트의 연출이 서버가 확정한 상태를 덮거나, 아이템 초기화·능력 Spec 도착 순서에 따라 사용 준비가 달라지는 위험. |
| 생성과 요청 수락이 분리되지 않음 | Spawn 후 StartEquipItem이 다시 검사하고 실패하면 반환한다. 호출자는 성공으로 처리하며 생성 아이템을 정리하는 결과 계약이 없다. | 거절 경로의 생성 액터 잔류 위험. 매번 발생하는 것으로 확인한 것은 아니다. |
| 같은 아이템 재선택 | ItemTag가 같으면 준비 상태를 확인하지 않고 성공 반환한다. | 이미 잘못된 연결/상태가 자동으로 복구되지 않는다. |

기존 로그에는 `Bow ... is missing required socket Arrow_socket`가 반복되어 있다. TryGetArrowSpawnTransform은 소켓이 실제로 없는 경우뿐 아니라 ArrowAnchorMesh 참조가 해제된 경우에도 false를 반환한다. 따라서 이 로그만으로 에셋의 소켓 누락이라고 판단하면 안 된다. 앞서 확인한 서버 발사 부재와 연결 해제 결함은 서로 부합하지만, 모든 피격·구르기 증상을 하나의 원인으로 확정한 상태는 아니다.

코드 근거:

- [장착 확정·취소·몽타주 처리](C:/Users/wonkii/Documents/GitHub/ArtisticSW2026/Source/ClassFeature/Private/Equipment/PlayerEquipmentComponent.cpp:840)
- [장착 중 일반 입력 차단](C:/Users/wonkii/Documents/GitHub/ArtisticSW2026/Source/ClassFeature/Private/BasePlayer.cpp:2078)
- [무기의 SourceObject·현재 장비·전환 상태 검사](C:/Users/wonkii/Documents/GitHub/ArtisticSW2026/Source/ClassFeature/Private/GAS/Ability/WeaponGameplayAbility.cpp:6)
- [활의 발사 위치 참조 검사](C:/Users/wonkii/Documents/GitHub/ArtisticSW2026/Source/ArtisticSWCore/Private/Item/Weapons/BowItem.cpp:103)
- [요청 정보를 전달하지 않는 장착 Notify](C:/Users/wonkii/Documents/GitHub/ArtisticSW2026/Source/ClassFeature/Private/Animation/AN_EquipmentAttachItem.cpp:6)

## 3. 장착·취소 정책

장착 확정은 서버가 새 아이템의 부착·발사 위치·능력·스탯을 확인하고 현재 장비로 게시하는 순간이다. 현재 코드처럼 유효한 장착 Notify에서 확정할 수 있으며, 애니메이션 전체 종료까지 반드시 기다릴 필요는 없다.

| 발생 시점 / 상황 | 권장 동작 |
| --- | --- |
| 확정 전 피격·Stun | 진행 중 요청 취소. 새 임시 무기와 새 요청의 능력·효과만 제거. 이전 장비·능력·스탯·발사 위치 복구. 피격/Stun이 끝나면 이전 무기를 사용 가능. |
| 확정 전 구르기 승인 | 구르기가 유효하고 실행이 승인된 뒤 장착 취소, 그 다음 구르기 몽타주 시작. 구르기 입력 거절만으로 장착을 취소하지 않음. |
| 확정 후 피격·구르기·Stun | 남은 장착 연출과 진행 중 공격/조준 행동을 해당 규칙에 따라 중단. 새 장비의 능력 Spec과 장비 스탯은 유지. 행동 제한 종료 후 다시 사용 가능. |
| 이전 장비가 없는 상태에서 확정 전 취소 | None으로 복귀. 새 임시 액터·능력·장비 효과가 남지 않음. |
| 장착 재생·부착·능력·스탯 적용 실패 | 실패 이유 기록 후 동일한 취소/복구 경로로 종료. Equipping에 머물지 않음. |
| 사망·플레이어 종료 | 진행 요청을 무효화하고 임시·확정 장비의 소유 자원을 정리. 이전 장비를 다시 복구하지 않음. |
| 이전 요청의 늦은 Notify/몽타주 종료 | 요청 번호와 재생 인스턴스가 다르면 무시. 새 요청을 취소하거나 확정하지 않음. |

GAS에서 행동 취소와 능력 부여/회수는 별개로 관리한다. 피격 때문에 장비의 능력을 회수한 뒤 다시 부여하는 방식은 피한다. [Epic의 Gameplay Ability 수명 주기 문서](https://dev.epicgames.com/documentation/unreal-engine/using-gameplay-abilities-in-unreal-engine)

## 4. 단계별 구현 순서

### 단계 1 — 재현 기록과 활 연결 결함 수정 (최우선)

1. 장착 이벤트에 요청 번호, 서버/클라이언트 역할, 이전/대기/확정 아이템, State, 취소 사유를 기록한다. Begin/Notify/Commit/Abort/Failed 종료 이벤트를 중심으로 남기고 매 프레임 출력하지 않는다.
2. 이전 장비 정리는 명시적인 아이템을 받아 그 아이템의 능력·효과·발사 위치만 제거하도록 바꾼다. 이전 장비가 검이면 활 연결 해제를 호출하지 않는다.
3. ClearBowArrowAnchor의 '지정 활 해제'와 '전체 해제'를 서로 다른 함수로 나눈다. nullptr가 전체 해제를 뜻하는 기본 인자를 제거한다. 전체 해제는 사망·최종 장비 제거 같은 명시적 경로에서 사용한다.
4. 부착과 활 연결도 분리해, 취소 가능한 새 아이템의 외형 부착이 기존 확정 활의 연결을 해제하지 않도록 한다. 새 활의 연결을 검증한 뒤 확정 시 연결 소유자를 바꾼다.
5. 활 발사 실패 사유를 AnchorUnbound / CharacterMeshInvalid / SocketNameNone / SocketMissing으로 구분한다. 검→활 정상 교체만으로 기존 발사 실패를 재현하고 수정 후 비교한다.

완료 조건: 검→활에서 서버 TryGetArrowSpawnTransform이 성공하며, 이전 검 정리 후에도 같은 새 활의 연결이 유지된다. 능력 Spec이 없는 경우와 발사 위치만 없는 경우가 로그에서 구분된다.

### 단계 2 — 성공·취소·실패 종료 경로 통합

1. 서버 장착 요청에 증가하는 RequestId와 PreviousItem, PendingItem, MontageInstanceId, 내부 단계, 종료 결과를 둔다. 내부 단계는 Preparing / AwaitingAttach / Committing / Committed / Aborted로 구분한다. 기존 Blueprint용 EquipmentState 인터페이스는 유지할 수 있다.
2. BeginEquip의 결과를 반환한다. 요청을 수락하지 못하면 이 요청에서 생성한 아이템을 정리한다. '요청 수락'과 '장착 완료'를 구분한다.
3. TryCommitEquip(RequestId), AbortEquip(RequestId, Reason), FinishPresentation(RequestId)로 종료를 모은다. 같은 종료 호출이 반복돼도 정리와 브로드캐스트가 한 번만 실행되게 한다.
4. Mesh/AnimInstance 부재와 Montage_Play 실패를 즉시 처리한다. 몽타주가 지정됐지만 재생할 수 없는 경우에는 이전 장비로 복구한다. 원래 몽타주가 없는 장비·수영의 즉시 확정 경로는 별도로 유지한다.
5. 종료 콜백은 RequestId와 실제 재생 인스턴스를 캡처해 검증한다. Notify에도 애니메이션 이벤트의 출처를 전달해 현재 요청의 재생 인스턴스에서 나온 유효한 이벤트인지 확인한다. 단순히 이벤트 수신 시 현재 RequestId를 읽는 방식은 오래된 이벤트를 걸러내지 못한다.
6. EndDelegate 해제와 해당 장착 몽타주만 중단하는 처리를 추가한다. 정리 중 발생하는 종료 콜백은 종료된 요청으로 무시한다. 전부 중단하는 Montage_Stop으로 피격·구르기 몽타주까지 멈추지 않는다.
7. 정상 완료·Notify 누락 처리 정책을 명시한다. 현행 호환을 위해 유효한 정상 종료에서 1회 확정을 허용하되 누락을 진단한다. 재생도 종료 이벤트도 오지 않는 요청에는 재생 길이를 기준으로 한 제한 시간을 두고 실패 복구한다. 고정 지연을 장착 순서 보정 수단으로 사용하지 않는다.
8. PendingItem 파괴, ASC 변경, 사망, EndPlay도 같은 요청 무효화·자원 정리 경로를 사용한다.

완료 조건: 모든 요청이 Committed 또는 Aborted 중 하나로 종료되고, 실패 후 입력을 막는 Equipping·delegate·임시 액터가 남지 않는다.

### 단계 3 — 확정 순서와 복구 보장

권장 순서:

1. 이전 장비를 살아 있는 상태로 보존하고 새 장비 정의·소켓·ASC·능력 집합을 사전 검사한다.
2. 새 아이템 부착과 발사 위치를 검증한다. 확정 전에는 새 장비의 공격을 허용하지 않는다.
3. 새 장비의 능력과 효과를 요청 소유로 준비하고 적용 결과를 확인한다. 스탯 교체에 필요한 이전 아이템·효과 정보도 보존한다.
4. 효과 적용 등 동기 콜백을 발생시키는 단계 이후마다 요청 유효성과 사망·취소 요구를 재확인한다. Committing 중 재진입한 취소는 기록해 안전한 지점에서 처리한다. 사망은 최우선이다.
5. 준비가 끝나면 현재 장비, 장비 상태, 확정 Revision을 함께 갱신한다. 이 순간부터 일반 피격·구르기는 장착을 되돌리지 않는다.
6. 이전 아이템에 속한 능력·효과·활 연결만 정리하고 이전 액터를 파괴한다. 새 장비를 건드리는 StoreCurrentEquippedItem의 범용 상태 변경을 이 단계에 재사용하지 않는다.
7. UI 변경 알림은 일관된 확정 또는 취소 결과가 만들어진 뒤 한 번 보낸다.

확정 전 실패 시 새 요청의 자원만 제거하고 이전 부착·능력·스탯을 복구한다. 현재 Stats.Equip은 이전 스탯을 제거하므로, 새 장착 전체가 이후 중단되는 경우를 위한 복구 정보 또는 준비/확정 API가 필요하다. 그저 함수 호출 순서만 바꿔서는 전체 복구가 보장되지 않는다. 실패 반환값을 모두 처리하고, 이전 장비를 파괴한 뒤 복구를 시도하는 순서는 피한다.

RestoreRoomEquippedItem도 같은 준비·확정·실패 정리를 사용한다. 빈 장비 복원은 명시적 제거로 처리하고, 유효하지 않은 복원 아이템이 생성된 경우 반드시 정리한다. ASC 재초기화와 리스폰에서는 보존된 장비 데이터와 서버 Revision을 기준으로 다시 연결한다.

완료 조건: 서버의 현재 장비, 능력 SourceObject, 스탯 소유 아이템, 활 발사 위치의 소유자가 같고, 실패하면 이전 장비 또는 명시적인 None 중 하나로 복귀한다.

### 단계 4 — 피격·구르기·Stun과 네트워크 동기화

1. CanChangeEquipment에 Rolling / Damaged / Stun / ActionsBlocked 등 시작 금지 조건을 통합한다. 공격·활 당김 등 기존 제한도 유지한다.
2. 피격/Stun 진입 시 EquipmentComponent에 중단 사유를 알린다. 몽타주 교체로 들어오는 bInterrupted만 기다리지 않는다. 같은 이벤트에서 두 번 정리되어도 안전하도록 단계 2의 종료 함수를 사용한다.
3. 구르기를 장착 중 허용하는 정책을 적용한다면, 일반 입력의 전환 상태 차단에서 구르기 입력만 별도로 판정한다. GA 승인 후 장착 취소와 구르기 실행을 순서대로 연결한다. BP/직접 활성화 경로도 같은 연결을 사용한다.
4. 구르기·피격·Stun 종료 후 남아 있는 상태 태그와 bIsDodging/bIsHitReacting을 검사한다. 장비 정상 여부와 행동 제한 여부를 별도로 진단한다. 태그를 강제로 지워 사용을 복구하는 편법은 사용하지 않는다.
5. 서버만 장착 확정·능력 부여/회수·아이템 파괴를 결정한다. 클라이언트는 몽타주·외형을 예측하되 서버 장비 상태를 로컬 연출로 변경하지 않는다.
6. State / CommittedItem / Revision을 하나의 장착 상태 스냅샷으로 복제한다. 기존 EquippedItem 접근은 이 상태를 읽는 호환 경로로 정리한다. 몽타주 메시지에도 RequestId/Revision을 포함해 늦은 시작·종료 메시지를 무시한다.
7. 아이템 정의 초기화와 ASC 능력 Spec 복제는 별도로 도착할 수 있다. 상태 OnRep, 아이템 초기화, 능력 목록 변경 시 공통 ReconcileEquipment를 호출한다. '보임'과 '사용 준비 완료'를 구분하고 필요한 SourceObject가 일치할 때 준비를 완료한다.
8. 클라이언트가 구르기를 예측해 연출을 취소했으나 서버가 구르기를 거절한 경우, 서버의 진행 요청/확정 스냅샷에 맞춰 장착 연출을 복구한다. 로컬 취소만으로 서버 장비 능력을 제거하지 않는다.

같은 아이템을 재선택하면 readiness를 확인한다. 서버 장비가 정상인데 로컬 초기화가 늦은 경우에는 재연결만 수행한다. 서버 장비 자체가 불완전하면 명시적인 복구 요청으로 처리하며 단순히 같은 ItemTag라는 이유로 성공 반환하지 않는다.

완료 조건: 피격·구르기·Stun이 끝난 뒤 정상 장비를 사용 가능하고, 복제 순서나 예측 거절 때문에 영구적인 장착 중 상태가 남지 않는다.

### 단계 5 — 회귀 테스트와 실제 게임 검증

기존 ArtisticSW.Weapon.EquipmentLifecycle은 능력 부여/회수, SourceObject, 파괴·사망을 검사한다. 장착 몽타주 도중 취소·재진입·요청 교차는 추가 검증이 필요하다.

| 검증 | 합격 기준 |
| --- | --- |
| 무장 없음→검/활, 검→활, 활→검, 활→활 | 새 장비만 현재 장비이며 이전 장비 자원이 없음. 활 연결이 새 활을 가리킴. |
| Notify 이전 / 같은 프레임 / 확정 이후 피격·구르기·Stun | 앞의 정책표대로 이전 장비 복구 또는 새 장비 유지. 서버가 처리한 순서와 결과가 기록됨. |
| 취소 직후 같은 몽타주로 새 장착 | 이전 Notify/종료 콜백이 새 요청에 영향을 주지 않음. |
| 애니메이션·소켓·능력·스탯 실패 주입 | 모든 실패가 종료되고 임시 액터·Spec·효과·delegate가 남지 않음. 이전 장비 스탯도 복구됨. |
| 확정 중 효과 콜백에서 피격·사망·장비 파괴 발생 | 재진입으로 새 장비가 파괴된 뒤 다시 Equipped로 게시되지 않음. 사망 후 무기가 다시 복구되지 않음. |
| 같은 장비 재선택, 복원 실패, ASC 재초기화 | 준비 상태 일치, 중복 부여 없음, 실패 생성 액터 잔류 없음. |
| 클라이언트 예측 구르기 승인/거절, 복제 지연 | 서버 결과로 수렴. 임시 로컬 연출 상태가 권위 장비 상태를 덮지 않음. |

구현 후 Editor 빌드와 관련 Weapon·Strength 장비 자동화 테스트를 실행한다. 동일 스레드의 Notify→Hit와 Hit→Notify 순서를 직접 호출하는 테스트, 효과 콜백 재진입 테스트를 추가해 경계 상황을 재현한다. 실제 애니메이션 슬롯·Notify의 동작은 별도 PIE 검증으로 확인한다.

PIE는 Listen Server와 Dedicated Server + Client에서 수행한다. 네트워크 지연 0/100/200ms, 패킷 손실 0/2% 조건으로 핵심 교체 시나리오를 각각 20회 반복한다. 육지·일반 배·DebugShip_M_3에서 검↔활 전환과 피격·구르기를 비교한다.

활 검증은 서버의 발사 승인·Projectile Spawn·FirstStep 로그를 기준으로 한다. 초록 경로 표시도 확인하되 서버 디버그 표시 설정에 따라 안 보일 수 있으므로 표시만으로 성공을 판단하지 않는다. 발사가 정상화된 후 남는 빗나감은 배 속도·조준 보정·충돌 문제로 따로 검증한다.

최종 합격 기준:

- 취소/실패 종료 후 장착 전환 상태가 남지 않는다.
- 살아 있는 현재 장비와 그 장비의 능력 SourceObject·장비 스탯이 일치한다.
- 장비에 필요한 능력 수가 정의와 같고 이전/임시 장비의 잔여 Spec·효과가 없다.
- 정상 활의 서버 발사 위치 연결이 유지된다.
- 행동 제한 종료 후 검 공격·활 조준·발사를 다시 사용할 수 있다.
- 실패와 취소가 요청 번호 및 사유 하나로 설명되며, 재현 테스트 전체에서 장비 사용 불가 상태가 반복되지 않는다.

## 5. 수정 대상과 우선순위

| 우선순위 | 주요 대상 | 변경 내용 |
| --- | --- | --- |
| P0 | PlayerEquipmentComponent, BowItem, GA_BowAimFire | 아이템별 연결 정리, 실패 원인 구분, 생성/재생 실패 시 종료 |
| P1 | PlayerEquipmentComponent, EquipmentStatComponent, AN_EquipmentAttachItem | 요청 식별, 확정·취소 통합, 이전 장비 복구, 재진입 방어 |
| P1 | BasePlayer, GA_PlayerRoll, GA_PlayerHitReaction / 상태 태그 연결 | 입력·행동 허용 기준 통합, 승인된 행동과 장착 중단 순서 |
| P2 | 장비 상태 복제, WeaponGameplayAbility, 능력 입력 연결 | 서버 상태와 연출 분리, 도착 순서에 따른 준비 상태 재조정 |
| 각 단계 공통 | WeaponEquipmentTests, StrengthCombatTests, 네트워크 PIE | 해당 변경의 회귀 검증 후 다음 단계 진행 |

가장 먼저 검→활에서 발사 위치가 지워지는 결함과 재생 실패 시 Equipping이 남는 경로를 수정한다. 이후 취소·확정 경계를 정리하고 피격·구르기 및 복제 처리를 연결한다. 장착 시스템 전체를 새 Gameplay Ability로 옮기는 큰 변경은 이 문제 해결의 필수 조건이 아니다.
