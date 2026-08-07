# 스토리 상태 시스템

## 핵심 개념

스토리 진행은 `EStoryNode`의 집합으로 저장한다.

- 노드는 이미 일어난 사건이다.
- `CompleteStoryNode(Node)`는 사건이 일어났다고 기록한다.
- `IsStoryNodeReached(Node)`는 그 사건이 기록되어 있는지 확인한다.
- 완료된 노드는 저장, 불러오기와 2인 협동 플레이 동기화 이후에도 유지된다.
- 노드의 enum 선언 순서나 화면상의 위아래 위치에는 의미가 없다.
- 진행 가능 여부는 아래 화살표 의존성만으로 결정한다.

예를 들어 서버에서 다음을 호출하면:

```cpp
Story->CompleteStoryNode(EStoryNode::SupplyPatrolQuestAccepted);
```

그 이후에는 항상 다음 결과가 나온다.

```cpp
Story->IsStoryNodeReached(EStoryNode::SupplyPatrolQuestAccepted); // true
```

스킬 시스템, 적 시스템, NPC 시스템이 이 노드 하나를 각자의 조건으로 사용할 수 있다.

## 현재 스토리 노드와 화살표

```text
GameStarted
└─ FirstSailingCompleted
   └─ ReconQuestAccepted
      ├─ StoryClue1Acquired
      ├─ CipherBookAcquired ───────────────────────────────┐
      └─ MiddleBoss1Defeated                               │
         └─ SupplyPatrolQuestAccepted                      │
            ├─ CurrentGeneratorUnlocked                    │
            └─ MiddleBoss2Defeated                         │
               └───────────────────────────────────────────┤
                                                           │
               SuppressJapaneseForcesQuestAccepted ◀───────┘
               ├─ StormUnlocked
               └─ MiddleBoss3Defeated
                  └─ UldolmokBattleQuestAccepted
                     ├─ FlamethrowerUnlocked
                     └─ FinalBossDefeated
                        └─ EndingDialogueCompleted
```

`SuppressJapaneseForcesQuestAccepted`는 다음 두 화살표를 모두 요구한다.

- `MiddleBoss2Defeated`
- `CipherBookAcquired`

이 노드는 스크린샷의 “해독된 암호 제작, 이순신과 대화, 일본군 저지 퀘스트
수락”을 하나의 사건으로 표현한다.

## 노드가 의미하는 묶음 사건

보스 처치와 보스의 고정 스토리 아이템 획득은 하나의 사건으로 취급한다.

| 노드 | 함께 일어난 것으로 보는 사건 |
|---|---|
| `MiddleBoss1Defeated` | 중간보스 1 처치 + 명량 참공 지도 획득 |
| `MiddleBoss2Defeated` | 중간보스 2 처치 + 일본군 암호 획득 |
| `MiddleBoss3Defeated` | 중간보스 3 처치 + 일본군 증원 정보 획득 |
| `SuppressJapaneseForcesQuestAccepted` | 해독된 암호 제작 + 대화 + 일본군 저지 퀘스트 수락 |

실제 월드 아이템 드롭은 적 모듈이 기존 방식으로 처리한다. Story 모듈은 해당
묶음 사건이 완료되었다는 사실만 저장한다.

## 같은 노드를 여러 시스템이 사용하는 예

`SupplyPatrolQuestAccepted`가 완료되면:

- 적 개발자는 중간보스 2 스포너의 시작 조건으로 사용한다.
- 적 개발자는 보급로 일반 적의 표시 조건으로 사용한다.
- 스킬 개발자는 해류 발생기 해금 가능 조건으로 사용한다.

보급로 적 처치는 스토리 노드가 아니다. 적이 나타나는 시점만
`SupplyPatrolQuestAccepted`에 연결되고, 처치 결과는 메인 스토리에 영향을 주지 않는다.

## 잘못된 순서의 완료 요청

`CompleteStoryNode`는 화살표 선행 노드를 검사한다.

```cpp
// FirstSailingCompleted가 아직 아니라면 false이며 기록되지 않는다.
Story->CompleteStoryNode(EStoryNode::ReconQuestAccepted);
```

이미 완료된 노드를 다시 완료하면 `true`를 반환한다. 따라서 완료 보고는 안전하게
여러 번 호출할 수 있다.

## 공유 상태와 권한

- 스토리 상태는 플레이어별 상태가 아니라 캠페인 하나의 공유 상태다.
- `CompleteStoryNode`, `StartNewCampaign`, 저장과 불러오기는 서버에서 호출한다.
- `IsStoryNodeReached`는 서버와 클라이언트 모두 사용할 수 있다.
- 서버의 변경은 `AStoryStateReplicator`를 통해 모든 플레이어에게 전달된다.

## 저장

저장 데이터에는 완료된 노드 전체가 들어간다. 불러온 후에도 동일한
`IsStoryNodeReached` 결과가 복원된다.

새로운 스토리 사건이 필요하면 `EStoryNode`에 항목을 추가하고,
`ArePrerequisitesReached`에 화살표 조건을 추가한다. 외부 모듈은 Gameplay Tag나
내부 저장 구조를 수정하지 않는다.
