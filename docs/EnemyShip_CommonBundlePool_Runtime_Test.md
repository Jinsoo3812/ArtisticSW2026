# EnemyShip 공통 BP 풀 런타임 확인

2026-10-12 변경. Editor 재시작 후 최신 DLL로 실행한다.

2026-10-22 정합성 보완: 네이티브 `BundleCapacity` 기본값은 현재 12다. 아래 8척 수량 예시는 `BundleCapacity=8`로 설정했을 때의 예시이며 고정 상한이 아니다. 공통 BP의 고정된 갑판 형상은 reserve 최초 BeginPlay에서 한 번 준비하고, 할당/복원 시 그래프를 재사용한다. 일반 선원은 fresh 준비에서만 생성하고 복원 준비는 저장된 상태를 초기화하지 않는다.

현재 상태(2026-10-12 후속 요청): 아래 풀링 진단 로그와 전체 프로필 검사 호출은 주석 처리해 껐다. 새 진단 옵션은 만들지 않았다. `-SWRoomSaveAudit`는 독립적인 상세 저장 감사 옵션이며 그대로 동작한다. 갑판/선원 준비 및 fresh 할당의 필수 준비 성공 검사는 유지한다. 아래 로그 설명은 진단을 다시 사용할 때의 참고 자료다.

최신 동작(2026-10-12 Idle 정책 수정): 가까운 배는 AI Idle이어도 모두 Actor로 유지한다. 기본 8은 현재 Actor + 풀 대기 묶음의 합계이며 필요한 Actor가 8개를 넘으면 가까운 배/전투 중/복귀 중 배 모두 유지한다. 기지 복귀 및 Idle 전환 시 일반 선원·보스·각 Controller를 사망 처리 없이 삭제한다. 기존 소환 규칙은 다음 접근 시 재사용한다. Idle + Home 도착 + 기존 거리 밖이면 ISM/풀 반환 경로로 진행한다. 선원 생존/출현 대기/잔여 시야가 원거리 Idle 반환을 영구 차단하지 않는다. 스토리 gate가 닫힌 원거리 Idle 배도 거리 평가에서 제외하지 않으며, gate의 숨김/소환 규칙 자체는 유지한다.

화면에서 가까운 Idle 배의 부력 움직임을 확인하고, 멀어졌을 때 기지 ISM으로 전환되는지 확인한다. 여러 그룹을 오간 뒤 같은 그룹을 다시 접근해 공통 BP 재사용 및 원본 프로필 적용을 확인한다. 이때 진단 로그는 계속 꺼져 있다.

후속 요청으로 **수량 요약만 재활성화**했다. `[EnemyShipPool] Event=Initial/Changed`가 상태 변화 시 출력된다. `Active`는 현재 사용하는 BP 묶음 수(AI Idle 포함), `IdlePool`은 풀 대기 BP 수, `Bundles`는 둘의 실제 합계, `Capacity`는 기본 합계 목표 8이다. 전체 프로필 재파싱 및 개별 배 검증은 계속 꺼져 있다.

최신 요청으로 수량 요약 호출도 다시 주석 처리했다. 현재 `Initial/Changed` 수량 로그는 출력하지 않는다.

공통 `/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip.BP_EnemyShip_C` 묶음에 배치별 프로필을 적용한다. `BundleCapacity=8`은 **활동 + 유휴 실제 묶음**의 기본 수량이다. 활동 수가 8을 넘으면 필요한 만큼 생성하고, 반납 시 초과분을 제거한다. 논리 배치와 휴면 ISM은 독립적으로 남는다.

처음부터 배치되어 활동 중인 파생 BP와 이전 저장의 활성 파생 BP는 전투/저장 상태를 유지한다. 첫 휴면 반납 시 `RetireAuthoredBundle`로 제거하고, 다음 깨우기부터 공통 BP를 사용한다. 레벨이 같은 공통 묶음끼리 재사용한다. 이는 스트리밍 레벨에 속한 종속 Actor가 다른 레벨 해제로 사라지는 것을 피하기 위한 조건이다.

## 로그 필터

서버 Output Log 또는 `Saved/Logs`에서 `EnemyShipPool`을 검색한다. 변경 로그는 기본 Display 레벨로 출력하므로 별도 콘솔 활성화가 필요 없다.

| 로그 | 확인 항목 |
| --- | --- |
| `Event=Assigned` | `Allocation=Reused/Created/Existing`, `Profile`, `BundleClass`, `Actor`, `Id`, `Assignment`, `Squad`, `Archetype` |
| `Event=Returned` | 휴면 반납, `Retained`, 물리/틱/숨김 상태 및 귀환 위치 |
| `Event=RetireAuthoredBundle` | 기존 파생 BP가 공통 묶음 정책으로 전환되는 첫 반납 |
| `Event=ReserveCreated` | 활동 수가 8보다 적을 때 기본 수량까지 유휴 묶음 확보 |
| `Event=Trim` | 총 묶음 수 초과분 실제 해제 |
| `Event=CrewProfileChanged` | 선원 클래스·능력치 테이블/행·슬롯 구성이 달라져 기존 선원 풀 제거 |
| `Event=Changed/Initial` | `Active`, `IdlePool`, `Bundles`, `Capacity`, 논리 `Total`, `Dormant`, `ISMShips` |
| `EnemyShipPoolAudit` | 서버/클라이언트 휴면·깨우기, 중복 표시, 물리, ISM Transform 검사 |
| `Event=WakeFailed/ReserveFailed` | 설정 적용 또는 생성 실패 사유 |

`Total`은 논리 배치 수다. 실제 Actor 묶음 수는 `Bundles`로 확인한다. 평가 주기 0.5초 후 상태가 안정됐을 때 비교한다.

## 수동 시나리오

1. 거리 휴면 조건을 충족한 배들을 반납시킨다. 기존 파생 BP에는 첫 반납에 `RetireAuthoredBundle`이 나타난다.
2. 활동 5척만 유지하면 `Active=5 IdlePool=3 Bundles=8`인지 확인한다.
3. 휴면 3척을 깨우면 `Active=8 IdlePool=0 Bundles=8`인지 확인한다.
4. 9번째를 깨우면 `Allocation=Created`, `Active=9 IdlePool=0 Bundles=9`인지 확인한다.
5. 공통 BP 9척을 모두 휴면으로 반납시킨다. 초과 1묶음에 `Trim`이 발생하고 최종 `Active=0 IdlePool=8 Bundles=8`이어야 한다.
6. 서로 다른 원본 프로필의 배를 번갈아 깨운다. `Actor`가 재사용되는 동안 `Profile/Squad/Archetype/Home`이 새 배치와 일치하는지 확인한다. 선원 종류/수, 보스·스토리 조건, 상자 설정, 대포, 귀환 지점/오프셋을 실제 동작으로 확인한다.
7. 휴면/활동 혼합 상태를 저장하고 다시 불러온다. 공통 BP 활성 상태와 기존 파생 BP 저장 모두 클래스 계약 오류 없이 복원되는지 확인한다. 유휴 묶음 자체는 저장 대상이 아니며 복원 완료 후 기본 수량을 다시 확보한다.
8. 클라이언트에서 실제 배와 ISM이 동시에 보이지 않는지, 휴면 물리/틱이 정지하고 깨우기 때 회복되는지 확인한다.

코드 자동 테스트는 총량/초과 Actor 제거, 선원 프로필 교체, 기존 선원·보스·상자 동작을 검사한다. 실제 48척 배치의 거리 휴면, 네트워크 표시 및 저장 왕복은 위 런타임 확인으로 검증한다.

## 2026-10-12 갑판·선원 준비 수정 후 확인

최신 Editor DLL로 재실행한다. 풀 로그는 별도 옵션 없이 출력되며, 상세 저장 감사가 필요하면 기존 `-SWRoomSaveAudit`를 유지한다.

현재 코드에서는 휴면 reserve의 BeginPlay에서도 고정 갑판 그래프를 한 번 준비한다. 이 초기 동기 작업은 전환 큐의 프레임 예산에 포함되지 않는다. 전체 프로필 적용 후 fresh encounter reset과 선원 생성은 실제 활성화 전에 수행한다. `PrepareBundleDeck(false)`는 기존 그래프와 저장된 crew identity/encounter generation/slot progress를 보존한다. 준비 중 AI/물리/틱은 정지하고 필요한 actor Query collision만 잠시 허용한 뒤 복구한다. 준비 실패 시 해당 깨우기를 취소한다. 이전의 "휴면 BeginPlay에서 갑판 계산까지 미룸" 설명과 테스트 기대는 이 최초 1회 캐시 정책 이전의 기록이다.

Idle 전환은 단순 충돌 표시 변경이 아니다. 귀환한 조우의 일반 선원·보스·Controller를 사망 보상 없이 정리하고 이전 표적을 해제한다. 새 접근에서는 실제 Sight 조건으로 표적과 선원 배치를 다시 결정한다. 충돌 응답을 검사하려고 Idle 이벤트를 인위적으로 발생시킨 테스트는 전투/회피 검사 전에 표적을 다시 설정해야 한다. 이 테스트 순서 문제를 해결하려고 실제 Idle 정리나 Sight 제한을 제거하지 않는다. 공통 Actor 재사용 뒤 회피 우선순위는 할당된 논리 배치 이름을 사용한다.

| 로그 | 기대값/의미 |
| --- | --- |
| `Assigned Allocation=Reused` | 동일 Actor의 `PreviousId`와 새 `Id`가 서로 다른 배치로 바뀌는지 확인 |
| `ProfileCheck Scope=ImportedProfile` | 전체 저장 editable profile의 import 후 실제 메모리 값 비교. `Mismatches=0` 기대 |
| `ProfileCheck Scope=EncounterConfig` | 조우 초기화/활성화 후 선택한 안정 설정의 재비교. SquadID, fallback, 귀환 지점/오프셋, DeckChestSettings, SpawnPlan, 갑판 surface, 보스 설정 등을 확인. 파생 runtime/presentation 필드는 이 단계의 비교 대상이 아님 |
| `ProfileMismatch` | 불일치 Object/Field/Expected/Actual 출력. 긴 값은 로그에 512자까지 표시하지만 비교는 전체 값으로 수행 |
| `ProfileValue` | 일치한 Actor 설정의 값 출력. 귀환 BP 변수와 상자 설정 확인에 사용 |
| `ArchetypeCheck` | `AuthoredMatch=1 EffectiveMatch=1` 기대. campaign gate가 닫혀 fallback을 사용하는 경우 `Fallback=1` |
| `DeckPrepared` | `WalkReady=1`, 준비 중 `Idle=1 Physics=0` 기대. `CreateCrew=0`은 저장 복원 준비이며 선원을 새로 생성하지 않음 |
| `CrewReady` | fresh 할당은 `Planned=Actual`, `Complete=1 RequireComplete=1`. restore 관측은 `RequireComplete=0`이며 복원된 기존 상태를 보존 |
| `CrewSlot` | 각 슬롯의 `ClassMatch=1 StatsMatch=1`. 예상/실제 클래스 및 능력치 테이블·행을 함께 표시 |

선원 수가 3명인 프로필의 fresh 할당에서 `Planned=3 Actual=3`을 확인한다. 클래스/능력치 변경 없이 count만 확인하지 않는다. 능력치 행 일치는 개별 버프를 포함한 최종 수치 전체를 비교했다는 뜻은 아니다.

이전 결함으로 선원 0명이 저장된 방을 복원할 때 저장값을 자동으로 새 조우 상태로 바꾸지 않는다. 복원용 준비는 선원 체력/사망/진행/상자 상태를 초기화하지 않는다. 이번 수정의 fresh 초기화 검증은 해당 배가 휴면 반납 후 다시 할당되는 구간을 대상으로 한다.

자동 테스트: 총 묶음 수 검사 + ShipCrewChest 10개, 합계 11개 통과. 신규 `Crew.PooledDeckPreparation`은 휴면 BeginPlay, Query 충돌의 임시 복구, fresh 선원 생성, restore 준비에서 crew identity/encounter generation/slot progress 보존을 확인한다. `Crew.ProfileReassignment`는 다른 선원 클래스로 변경한 뒤 idle deck 준비를 통해 실제 새 선원 생성을 확인한다.
