# Player 끼임 복구 설정 가이드

작성일: 2026-10-12. UE 5.7 / ClassFeature.

검증: 최종 UnrealHeaderTool 및 `ArtisticSW2026Editor / Win64 / Development` C++ 빌드 성공. 자동화 테스트와 PIE 실행은 하지 않았다.

`ABasePlayer` 자식 Blueprint에는 `StuckRecoveryComponent`와 `StuckRecoveryInputComponent`가 기본으로 붙는다. 코드 빌드 후 에디터를 열고 Player Blueprint에서 설정한다. 기존 수정 에셋은 이 구현에서 변경하지 않았다.

## 탈출 키 설정

가장 간단한 방법:

1. Player Blueprint에서 `StuckRecoveryInputComponent`를 선택한다.
2. `Escape Action`을 비워두고 `Escape Key`에 원하는 키를 지정한다. 기본값은 미지정이다.
3. `Show Guide`를 켜면 끼임 확정 시 기본 안내 UI와 홀드 게이지가 나타난다.

Enhanced Input을 사용하는 방법:

1. Boolean Input Action `IA_EscapeStuck`을 만든다.
2. Player의 기존 보행용 `DefaultIMC`에 원하는 키를 매핑한다.
3. `StuckRecoveryInputComponent.EscapeAction`에 IA를 지정한다. 지정된 경우 `EscapeKey`는 사용하지 않는다.
4. IA/매핑에 Hold, HoldAndRelease, Pressed, Pulse 트리거를 넣지 않는다. 기본 Boolean Down 입력의 `Started`와 `Completed/Canceled`를 사용한다. 홀드 시간은 컴포넌트의 서버 타이머 하나가 관리한다.

키를 끼임 확정 **후에 새로 눌러야** 홀드가 시작된다. 확정 전에 누르던 키의 시간은 인정하지 않는다. 도중에 놓으면 0으로 초기화된다. 목적지가 없거나 복구를 중단할 수 없는 GA가 활성 상태이면 이동하지 않고 실패 안내를 표시한다. 이때 키를 놓은 후 다시 눌러야 재시도한다. UI는 서버가 인정한 진행률을 표시하므로 네트워크 지연에 따라 약간 늦게 갱신될 수 있다.

탈출 키는 다른 공격·상호작용·UI 기능과 겹치지 않게 지정한다. 임시 직접 키 방식은 다른 입력 기능을 소비하지 않는다.

## 감지와 이동 설정

`StuckRecoveryComponent`의 `Detection`, `Escape` 구조체를 펼친다.

| 항목 | 기본값 | 의미 |
| --- | --- | --- |
| Enable Stuck Detection | true | 전체 시스템 활성화 |
| Detection Interval | 0.1초 | 감시 샘플 주기 |
| Movement Mode Monitor Duration | 2초 | 이동 모드 변경 후 감시 |
| Post Ability Monitor Duration | 1초 | GA 종료·몽타주 종료 후 감시 |
| Fallback Inactivity Duration | 5초 | 이동 입력·움직임이 없는 시간 |
| Fallback Probe Interval | 1초 | 정상 대기 중 저빈도 충돌 검사 |
| Blocked Input Duration | 0.75초 | 이동 의도/낙하에도 정체될 때 감시 시작 |
| Stuck Confirm Duration | 0.75초 | 비정상 증거가 지속되어야 하는 시간 |
| Stationary Speed | 30cm/s | 기본 0.1초 샘플에서 약 3cm 이동에 해당 |
| Penetration Inset | 2cm | 바닥의 정상 접촉을 제외하는 검사 캡슐 축소량 |
| Obstruction Probe Distance | 20cm | 수평 8방향 이동 가능성 검사 거리 |
| Hold Duration | 3초 | 서버가 요구하는 키 유지 시간 |
| Forward Distance | 150cm | yaw 기준 전방 탈출 거리 |
| Up Distance | 100cm | 월드 Z 방향 탈출 거리 |
| Max Escape Attempts | 8 | 기본 위치→추가 상단→전방 축소→상단→좌우 후보 순 탐색 |
| Max Landing Drop | 250cm | 목적지에서 아래로 확인할 최대 착지 거리 |
| Allow Penetration Recovery | true | 시작 캡슐이 파묻힌 경우 제한적인 복구 허용 |
| Max Penetration Exit Distance | 75cm | 역방향 경로의 마지막 침투 구간 상한 |
| Cooldown | 2초 | 성공 후 재감지 억제 |

전방/상단 거리는 우선 후보의 기준이다. 우선 후보가 막히면 전방 축소, 추가 상단, 좌우 후보를 사용하므로 성공 위치가 항상 정확히 같은 오프셋은 아니다. 지면 없는 후보는 버린다. 경로가 막히거나 깊은 침투로 제한을 넘으면 탈출이 실패할 수 있다. 복구 후 Falling으로 변경되어 자연스럽게 착지한다.

## GA와 의도적인 이동 제한 연결

피격 `UGA_PlayerHitReaction`과 구르기 `UGA_PlayerRoll`은 이미 `UAbilityTask_MonitorStuckRecovery`를 호출한다. GA가 정상 종료, 취소, 중단되면 Task의 `OnDestroy`에서 자기 감시 토큰만 해제하고 후속 감시 시간을 연장한다.

다른 Blueprint GA에서 `Monitor Stuck Recovery` 노드를 실행하고 `On Ready`에서 기존 몽타주/이동 실행으로 이어준다. On Ready는 서버와 예측 클라이언트 모두 실행된다. 실제 감시 등록은 서버에서만 한다. Task는 GA가 끝날 때 자동 정리된다. GA보다 짧은 구간만 감시하려면 Task의 `End Task`로 해제한다. 이동·충돌에 영향을 주는 GA에 연결하는 것이 적절하다.

Native GA에서는 실행 시작 시 다음 한 줄을 호출한다:

```cpp
UAbilityTask_MonitorStuckRecovery::MonitorStuckRecovery(this)->ReadyForActivation();
```

비-GA 시스템은 서버에서 `BeginMonitoring`의 FGuid를 보관하고 모든 종료 경로에서 `EndMonitoring`에 전달한다. 여러 요청의 토큰을 하나로 덮어쓰지 않는다. SourceAbility 없이 등록한 요청은 자동 GA 취소 대상이 아니므로 호출자가 이동 효과의 종료도 책임진다.

몽타주 재생 자체도 샘플 주기마다 관찰하므로 Task를 연결하지 않은 몽타주에 대한 감시 경로가 있다. Task 연결은 서버에서 애니메이션을 생략하는 경우와 Ability별 종료·취소 책임을 명확하게 하는 데 유용하다.

컷신, 별도의 `MOVE_None`, 커스텀 이동 잠금에서는 서버가 `SetDetectionSuspended(true)`를 호출하고 종료 시 false로 되돌린다. 중첩 잠금은 호출자가 최종 잠금 해제 시점을 관리한다. GAS 상태라면 `Suppression Tags`에 의도적인 이동 잠금 태그를 추가해도 된다. 기본 제외 태그는 `State.Dead`, `State.Status.Stun`, `State.Swimming`이다. 피격 상태는 감시 대상이므로 제외 태그로 추가하지 않는다.

사망, PlayerController가 이 Pawn을 소유하지 않는 상태, 탑승, 수영, 래그돌, 입력 무시, 생명주기 전환 중에는 복구를 허용하지 않는다. 잠금이 정상적으로 풀리지 않은 경우라도 무조건 탈출시키는 정책이 필요하다면 게임 규칙에 맞춘 추가 구분이 필요하다.

## UI 변경

기본 `UStuckRecoveryWidget`은 에셋 없이 동작한다. 화면 아래쪽에 키/시간 안내와 진행률을 표시하며 클릭을 가로채지 않는다.

다른 디자인이 필요하면 `UStuckRecoveryWidget` 자식 Widget Blueprint를 만들고 `Guide Widget Class`에 지정한다. Text는 `GetGuideText`, ProgressBar는 `GetProgress`를 사용한다. `Recovery`와 `KeyLabel`도 Blueprint에서 읽을 수 있다. 직접 HUD로 표시하려면 `Show Guide`를 끄고 Component의 `OnStateChanged`, `OnRecoveryResult`, `IsEscapeAvailable`, `GetHoldProgress`를 이용한다.

## 수동 확인 항목

자동화 테스트는 추가·실행하지 않았다. 아래는 실행 결과가 아닌 에디터에서 확인할 절차다.

1. 키를 지정하고 10초 이상 정상 대기한다. 감시는 시작되어도 탈출 안내가 나타나지 않아야 한다.
2. 일반 벽 한 면을 계속 밀고 정상 점프·낙하·착지를 한다. 끼임으로 확정되지 않아야 한다.
3. 피격 몽타주 또는 낙하로 캡슐이 지형 내부에 끼이는 실제 사례를 재현한다. 비정상 증거가 지속되면 안내가 나타나야 한다.
4. 이동 방향을 바꾸어 정상적으로 빠져나오면 안내가 사라져야 한다.
5. 탈출 키를 2초 누른 뒤 놓는다. 진행률이 0으로 돌아가고 이동하지 않아야 한다.
6. 3초 이상 누르면 안전한 후보로 이동하고 착지해야 한다. 전방/상단 값을 바꾸어 이동량을 확인한다.
7. 우선 후보를 벽으로 막는다. 다른 후보를 찾거나 실패해야 하며 새 장애물을 통과하면 안 된다.
8. 모든 후보의 공간/지면을 막는다. 위치 변경 없이 실패 안내가 나타나야 한다. 계속 누르고 있어도 자동 재시도하지 않아야 한다.
9. 감시 중 GA를 취소·중단하고 두 GA 감시를 중첩한다. 한 Task 종료가 다른 감시를 없애지 않아야 한다.
10. 스턴·죽음·수영·함선/대포 탑승·컷신으로 전환한다. 진행 중 홀드가 취소되고 위치가 복구되지 않아야 한다.
11. 움직이는 함선 갑판에서 침투 사례를 재현한다. 함선의 이동만으로 끼임 감지가 해제되지 않아야 한다.
12. listen server와 별도 클라이언트에서 키 해제, 실패 재시도, 복구 후 Root Motion 재적용 여부와 다른 클라이언트의 위치 동기화를 확인한다.

지형/함선 에셋의 실제 사례를 기준으로 임계값을 조정한다. 이 시스템은 피격 끼임 원인 자체를 고친다는 보장이 아닌 복구 수단이다.
