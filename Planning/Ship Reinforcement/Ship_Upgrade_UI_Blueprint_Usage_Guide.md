# 배 강화 UI Blueprint 사용 가이드

## 1. UI가 알아야 하는 객체

UI는 `UShipUpgradeComponent` 하나만 사용한다.

다음 구현 세부사항에는 직접 접근하지 않는다.

- 강화 Tree Data Asset 직접 검색
- Ship Data Table 행 검색
- GAS Attribute 직접 읽기/쓰기
- SaveGame 직접 로드/저장
- Server RPC 직접 호출

위젯의 `Construct` 또는 화면 진입 함수에서 다음 Blueprint 함수를 호출한다.

```text
Get Local Ship Upgrade Component
```

반환값이 유효하지 않으면 PlayerState가 아직 준비되지 않은 것이므로 짧게 기다린 뒤 다시 시도하거나 화면을 비활성 상태로 둔다.

기본 강화 트리는 다음 경로에서 자동으로 로드된다.

```text
/Game/New/Ship/Upgrade/DA_ShipUpgradeTree
```

PlayerState의 `ShipUpgradeComponent`에 다른 Tree를 지정하면 그 자산이 우선한다.

---

## 2. 화면 최초 구성

### 2.1 이벤트 바인딩

컴포넌트를 얻은 직후 다음 Delegate에 바인딩한다.

| 이벤트 | UI 처리 |
| --- | --- |
| `OnUpgradeDataReady` | 전체 노드 다시 생성 |
| `OnNodeStateChanged` | 해당 NodeId의 버튼 상태와 연결선 갱신 |
| `OnNodeActivationResult` | 로딩 표시 종료, 성공/실패 메시지 표시 |
| `OnShipStatsChanged` | 현재 총 스탯 패널 갱신 |
| `OnUpgradeDataChanged` | 재료 보유량 또는 강화 상태가 변했으므로 현재 View 재조회 |

위젯을 닫을 때 Delegate 바인딩을 해제한다.

### 2.2 전체 노드 생성

```text
UpgradeComponent
    → Get All Node Views
    → For Each Loop
        → Node Widget 생성
        → NodeId 저장
        → GraphPosition으로 Canvas Slot 위치 지정
        → DisplayName / Icon / State 적용
```

`GetAllNodeViews()`가 반환하는 배열 순서를 UI 의미로 사용하지 않는다. 영구 식별자는 항상 `NodeId`다.

### 2.3 연결선 생성

각 `FShipUpgradeNodeView`의 `PrerequisiteNodeIds`를 읽는다.

한 노드에 상위 노드를 N개 지정할 수 있다. N개는 모두 충족해야 하는 AND 조건이며, UI는 각 상위 노드에서 현재 노드로 연결선을 각각 그린다.

```text
현재 노드의 PrerequisiteNodeIds
    → 선행 NodeId 위젯 검색
    → 선행 위젯 중심에서 현재 위젯 중심까지 연결선 생성
```

노드 상태는 다음 세 종류다.

| 상태 | 권장 표현 |
| --- | --- |
| `Locked` | 어둡게, 활성화 버튼 비활성 |
| `Available` | 선택/활성화 가능 강조 |
| `Active` | 완료 색상과 체크 표시 |

---

## 3. 노드 선택 상세 패널

노드 버튼을 클릭하면 저장해 둔 `NodeId`로 다음 함수를 호출한다.

```text
Get Node View(NodeId)
```

성공하면 다음 값을 상세 패널에 적용한다.

- `DisplayName`
- `Description`
- `Icon`
- `State`
- `StatChanges`
- `MaterialCosts`
- `bHasEnoughMaterials`
- `UnavailableReason`

`StatChanges`의 `FormattedText`는 바로 TextBlock에 넣어도 되는 완성 문구다.

예:

```text
대포 공격력: 20 → 30 (+10)
다음 발사까지의 시간: 2초 → 1.8초 (-0.2초)
```

색상을 직접 정하려면 `bImprovesStat`을 사용한다. 쿨다운은 숫자가 감소할 때 이득이므로 Delta 부호만 보고 색을 정하면 안 된다.

### 3.1 재료 비용 표시

`MaterialCosts`의 각 행에는 다음 값이 이미 가공되어 있다.

| 필드 | 사용법 |
| --- | --- |
| `DisplayName`, `Icon` | 재료 이름과 아이콘 |
| `OwnedQuantity` | 현재 인벤토리 보유량 |
| `RequiredQuantity` | 노드 활성화 필요량 |
| `bEnough` | 해당 재료 충족 여부와 색상 |

배열이 비어 있으면 무료 노드다. `bHasEnoughMaterials`는 모든 재료 행의 충족 여부를 합친 값이다. 인벤토리가 변하면 `OnUpgradeDataChanged`가 오므로 선택 중인 노드에 `GetNodeView(NodeId)`를 다시 호출한다.

---

## 4. 활성화 버튼

버튼 활성 여부는 반드시 다음 함수를 사용한다. `State == Available`은 선행 노드 조건만 충족했다는 뜻이며 재료 부족일 수 있다.

```text
Can Activate Node(NodeId, OutReason)
```

버튼 클릭 흐름:

```text
활성화 버튼 클릭
    → 버튼 임시 비활성화
    → 로딩 표시
    → Request Activate Node(NodeId)
    → 결과를 기다림
```

`RequestActivateNode`의 실행 직후에는 성공으로 간주하지 않는다. 실제 판정은 서버가 하며 결과는 `OnNodeActivationResult`로 돌아온다.

결과 처리:

| Result | UI 처리 예시 |
| --- | --- |
| `Success` | 성공 효과 재생, 노드와 연결선 갱신 |
| `AlreadyActive` | 현재 상태 다시 조회 |
| `UnknownNode` | 데이터 오류 메시지 및 로그 |
| `MissingPrerequisite` | 반환 Message 표시 |
| `NotAuthority` | 네트워크 오류 메시지 |
| `SaveFailed` | 저장 실패 메시지, 활성화되지 않은 상태 유지 |
| `NotConfigured` | 시스템 설정 오류 표시 |
| `MissingMaterials` | 반환 Message 표시, 재료 행 강조 |
| `InvalidCost` | 기획 데이터 오류 표시 및 로그 |

더블 클릭이나 같은 요청의 재전송이 발생해도 서버는 한 노드를 두 번 적용하지 않는다.

---

## 5. 활성화 전 미리보기

선택한 노드가 활성화 가능할 때 다음을 호출한다.

```text
Get Stats After Activating(NodeId)
```

반환되는 `FShipStatSnapshot`은 해당 노드까지 활성화했다고 가정한 전체 최종 스탯이다.

현재 값은 다음 함수로 얻는다.

```text
Get Current Ship Stats
```

두 Snapshot을 나란히 표시할 수도 있고, 일반적인 상세 패널에서는 `GetNodeStatChanges()`의 결과만 표시해도 된다.

Snapshot 필드:

| 필드 | UI 표시 |
| --- | --- |
| `CannonDamage` | 대포 공격력 |
| `CannonFireCooldownSeconds` | 다음 발사까지의 시간 |
| `CannonballSpeed` | 대포 발사속도 |
| `MaxHealth` | 배 체력 |
| `ForwardPropulsionMultiplier` | 배 WS 추진속도 |
| `TurnTorqueMultiplier` | 배 AD 회전속도 |

추진과 회전 값은 현재 물리 힘/토크 배율이다. 퍼센트로 표시하려면 `1.0 = 100%`로 변환한다.

---

## 6. 권장 위젯 책임 분리

### `WBP_ShipUpgradeScreen`

- UpgradeComponent 획득
- Delegate 바인딩/해제
- 전체 Node View 조회
- NodeId → NodeWidget Map 관리
- 연결선 관리
- 현재 총 스탯 표시

### `WBP_ShipUpgradeNode`

- 자신의 NodeId 저장
- 이름, 아이콘, State 표시
- 클릭 시 상위 Screen에 NodeId 전달

### `WBP_ShipUpgradeDetails`

- 선택한 Node View 표시
- StatChanges 표시
- 활성화 버튼 상태 관리
- RequestActivateNode 호출
- 활성화 결과 메시지 표시

노드 위젯이 SaveGame이나 PlayerState를 개별 조회하지 않도록 한다. Screen이 컴포넌트 하나를 소유하고 하위 위젯에는 View 데이터만 전달하는 편이 단순하다.

---

## 7. 전체 Blueprint 유즈케이스

```text
[화면 열기]
    GetLocalShipUpgradeComponent
    Bind Events
    GetAllNodeViews
    Create Nodes and Connections

[Hull_I 선택]
    GetNodeView(Hull_I)
    Details에 체력 증가량 표시
    State가 Available이므로 버튼 활성

[활성화 클릭]
    RequestActivateNode(Hull_I)
    서버에서 모든 상위 노드와 재료 재검증
    Inventory 재료 원자적 차감
    SaveGame 저장
    ActiveNodeIds 복제
    OnNodeActivationResult(Success)
    OnNodeStateChanged(Hull_I, Active)
    OnShipStatsChanged

[후속 Cannon_I]
    선행 Hull_I가 Active이므로 Available로 변경
    MaterialCosts에 보유량/필요량 표시
    재료가 부족하면 버튼 비활성, 재료 획득 시 OnUpgradeDataChanged로 재조회
    활성화 후 대포 공격력 미리보기와 현재 수치 갱신

[전투 맵 진입]
    PlayerState가 저장된 ActiveNodeIds 복원
    플레이어 Ship이 기본 DT 행을 읽음
    ApplyPlayerUpgrades(PlayerState)
    동일 계산기로 최종 Snapshot 생성
    Ship GAS Attribute와 WS/AD 물리 배율에 적용
```

---

## 8. UI 디버깅 체크리스트

- 컴포넌트를 얻지 못함: 현재 PlayerController에 PlayerState가 복제됐는지 확인
- 노드가 0개: `DA_ShipUpgradeTree`가 설정/로드됐는지 확인
- 자식 노드가 계속 Locked: `UnavailableReason`을 화면 또는 로그에 출력
- 상위 노드가 여러 개인 자식이 안 열림: `PrerequisiteNodeIds`의 모든 노드가 Active인지 확인
- Available인데 버튼이 꺼짐: `bHasEnoughMaterials`와 `UnavailableReason` 확인
- 재료 숫자가 갱신되지 않음: `OnUpgradeDataChanged` 바인딩 확인 후 `GetNodeView` 재호출
- 클릭 후 변화가 없음: `OnNodeActivationResult` 바인딩 여부 확인
- 미리보기와 실제 전투 수치가 다름: UI가 직접 계산하지 않았는지 확인
- 쿨다운 개선이 나쁜 색으로 표시됨: Delta 부호 대신 `bImprovesStat` 사용
- 맵 이동 후 초기화됨: PlayerState/SaveGame 로드 완료 전에 UI를 그리지 않았는지 확인

---

## 9. 프로그래머에게 요청해야 하는 경우

다음 요구는 UI Blueprint에서 우회 구현하지 말고 API 확장을 요청한다.

- 새로운 배 스탯 종류 추가
- 새로운 비용 종류 또는 인벤토리 외 결제 수단 추가
- 여러 강화 트리 전환
- 노드 초기화/환불
- 전투 중 실시간 강화 적용 정책
- 온라인 계정 프로필 저장

UI에서 별도의 계산식이나 임시 SaveGame을 만들면 서버의 실제 전투 수치와 화면이 달라질 수 있다.

---

## 10. 기획자의 노드 설정 절차

`DA_ShipUpgradeTree`에서 노드마다 다음을 자유롭게 설정한다.

1. `PrerequisiteNodeIds`: 상위 노드 0~N개. 모두 활성화되어야 해금된다.
2. `ActivationCosts`: 필요한 `ItemTag`와 `Quantity` 0~N개. 모두 보유해야 하며 성공 시 전부 차감된다.
3. `StatModifiers`: 활성화 후 플레이어 배에 적용할 스탯 변경 1~N개.
4. `GraphPosition`: 하향식 트리 UI에서 사용할 좌표.

잘못된 상위 ID, 순환 참조, 중복 상위 ID, 잘못된 재료 태그/수량은 Data Asset Validation에서 검출된다. 실제 활성화 요청 때도 서버가 다시 검증하므로 UI 표시를 우회해 요청해도 재료 없이 해금되지 않는다.
