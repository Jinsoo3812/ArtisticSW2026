# 외부 모듈에서 Story 사용하기

## 외부 개발자가 기억할 API

일반 게임 코드는 `UStoryFacadeSubsystem`만 사용한다.

```cpp
// 사건 완료 보고
Story->CompleteStoryNode(EStoryNode::SupplyPatrolQuestAccepted);

// 사건 도달 확인
Story->IsStoryNodeReached(EStoryNode::SupplyPatrolQuestAccepted);
```

퀘스트, 보스, 기능별 별도 조회 API와 Story Gameplay Tag는 사용하지 않는다.

## 모듈 의존성

Story API를 `.cpp` 파일 안에서만 사용한다면 해당 모듈의 `Build.cs`에 추가한다.

```csharp
PrivateDependencyModuleNames.Add("Story");
```

모듈의 `Public` 헤더가 `EStoryNode`를 외부에 노출한다면
`PublicDependencyModuleNames`에 추가한다.

```cpp
#include "StoryFacadeSubsystem.h"
```

## 파사드 가져오기

```cpp
UStoryFacadeSubsystem* Story =
    GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>();

if (!Story)
{
    return;
}
```

## 조건 걸기

### 중간보스 2

```cpp
if (Story->IsStoryNodeReached(EStoryNode::SupplyPatrolQuestAccepted))
{
    SpawnMiddleBoss2();
}
```

중간보스 2가 죽었을 때 서버에서 완료를 보고한다.

```cpp
void AMiddleBoss2::HandleDeath()
{
    if (!HasAuthority())
    {
        return;
    }

    if (UStoryFacadeSubsystem* Story =
        GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>())
    {
        Story->CompleteStoryNode(EStoryNode::MiddleBoss2Defeated);
    }

    DropConfiguredItems();
}
```

`MiddleBoss2Defeated`에는 일본군 암호 획득도 포함된다. 실제 드롭 액터 생성은 기존
적 드롭 시스템이 담당한다.

### 보급로 일반 적

```cpp
const bool bSupplyEnemiesEnabled =
    Story->IsStoryNodeReached(EStoryNode::SupplyPatrolQuestAccepted);
```

보급로 적을 죽였을 때는 Story API를 호출하지 않는다.

### 해류 발생기

```cpp
if (Story->IsStoryNodeReached(EStoryNode::SupplyPatrolQuestAccepted))
{
    UnlockCurrentGeneratorInteraction();
}
```

실제로 NPC 대화를 통해 해류 발생기가 해금되면 서버에서 기록한다.

```cpp
Story->CompleteStoryNode(EStoryNode::CurrentGeneratorUnlocked);
```

### 퀘스트 수락

```cpp
void AYiSunSinNpc::AcceptSupplyPatrolQuest()
{
    if (!HasAuthority())
    {
        return;
    }

    if (UStoryFacadeSubsystem* Story =
        GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>())
    {
        Story->CompleteStoryNode(EStoryNode::SupplyPatrolQuestAccepted);
    }
}
```

## 에디터에서 코드 없이 연결하기

### 아직 존재하면 안 되는 보스

맵에 `AStoryConditionalSpawner`를 배치한다. 중간보스 2의 설정은 다음과 같다.

| 속성 | 값 |
|---|---|
| `Spawned Actor Class` | 중간보스 2 Blueprint |
| `Required Story Node` | `SupplyPatrolQuestAccepted` |
| `Stop After Story Node` | 켬 |
| `Stop After Story Node` 값 | `MiddleBoss2Defeated` |

퀘스트 수락 전에는 보스가 없고, 수락 후 나타나며, 처치 후에는 다시 나타나지 않는다.
처치 노드가 기록되는 순간 보스를 강제로 삭제하지 않으므로 죽음 연출과 아이템 드롭은
끝까지 실행된다.

### 맵에 미리 배치된 적, NPC, 장치

액터에 `UStoryStateGateComponent`를 추가하고 `Required Story Node` 하나를 고른다.

보급로 적과 해류 발생기 NPC는 다음처럼 설정한다.

```text
Required Story Node = SupplyPatrolQuestAccepted
```

노드 도달 전에는 액터가 숨겨지고 충돌과 Tick이 꺼진다. 도달 후에는 원래 설정으로
복구된다.

## 상태 변경 이벤트

UI처럼 진행이 바뀔 때 즉시 갱신해야 한다면 이벤트를 구독한 뒤 다시 조회한다.

```cpp
Story->OnStoryChanged.AddDynamic(this, &UMyWidget::RefreshStory);

void UMyWidget::RefreshStory()
{
    const bool bReached =
        Story->IsStoryNodeReached(EStoryNode::SupplyPatrolQuestAccepted);
}
```

## 저장과 불러오기

서버에서 호출한다.

```cpp
Story->SaveCampaign();
Story->LoadCampaign();
```

## 요약

외부 모듈의 규칙은 세 가지다.

1. 사건이 실제로 일어나면 서버에서 `CompleteStoryNode`.
2. 콘텐츠 조건은 `IsStoryNodeReached`.
3. 보스는 스포너에 시작 노드와 처치 노드를 지정한다.
