# 피격 Root Motion 서버·클라이언트 동기화 조사 및 처리 계획

작성일: 2026-09-08. 대상: ArtisticSW2026, Unreal Engine 5.7, GAS + CharacterMovementComponent(CMC).

## 결론과 확인 범위

현재 코드는 서버에서 피격 Ability와 몽타주를 실행하는 구조를 이미 갖추고 있다. 그러나 공통 피격 몽타주 재생 호출이 **Root Motion 이동 배율을 0으로 전달**한다. UE 5.7의 Ability Task는 Authority에 이 배율을 적용하지만, ServerInitiated Ability의 소유 클라이언트에는 적용하지 않는다. 이 경로에서 서버와 클라이언트의 이동 배율이 달라질 수 있다는 코드 결함을 확인했다.

우선 기존 GAS 몽타주 + CMC 내장 Root Motion 복제 경로를 정상화한다. 서버 전용 위치 이동 RPC나 클라이언트 위치 추종 시스템을 새로 만드는 것은 필요하지 않다.

이번 조사는 프로젝트 C++ 코드, 기존 테스트 소스, 설치된 UE 5.7 엔진 소스, Epic 공식 문서에 근거한다. 실행 중인 Blueprint의 최종 설정 조회, 최신 바이너리 확인, 멀티플레이 재현 및 런타임 로그 수집은 수행하지 않았다. 따라서 **코드 결함은 확인했지만, 신고된 순간이동의 유일한 원인이라고 확정하지는 않는다.** 아래 재현 단계에서 캡슐 이동과 Mesh만의 시각 이동도 구분한다.

## 확인한 코드 근거

| 위치 | 확인 내용 | 의미 |
| --- | --- | --- |
| `Source/GASCore/Private/Components/BaseHealthComponent.cpp:260` 부근 | Authority 확인 후 생존 중 체력 감소 시 피격 GameplayEvent 전송 | 서버에서 피격을 시작하는 경로가 존재한다. |
| `Source/GASCore/Private/Abilities/BaseHitReactionGameplayAbility.cpp:17` | `NetExecutionPolicy = ServerInitiated` | 서버가 시작하고 소유 클라이언트에서도 실행하는 정책이다. |
| 같은 파일 `:109–116` | `CreatePlayMontageAndWaitProxy(..., bStopHitReactionMontageWhenAbilityEnds, 0.0f)` | 일곱 번째 인수는 시작 시간이 아니라 `AnimRootMotionTranslationScale`이다. |
| UE 5.7 `AbilityTask_PlayMontageAndWait.h:64–71` | 이동 배율 0은 Root Motion translation 차단, 시작 시간은 그 다음 인수 | 호출 인수의 의미를 실제 엔진 버전에서 확인했다. |
| UE 5.7 `AbilityTask_PlayMontageAndWait.cpp:163–167` | Authority 또는 LocalPredicted AutonomousProxy에만 배율 설정 | 현재 ServerInitiated에서는 서버에 0이 설정되고 소유 클라이언트는 해당 설정을 건너뛴다. 클라이언트의 기존 배율은 런타임 확인 대상이다. |
| UE 5.7 `CharacterMovementComponent.cpp:11830–11844` | 추출한 Root Motion에 캐릭터의 이동 배율을 곱함 | 서버가 몽타주를 재생해도 translation이 0이 될 수 있다. 회전 차단과는 별개다. |
| `Source/ClassFeature/Private/GAS/Ability/GA_PlayerHitReaction.cpp:70–81` | `Montage->HasRootMotion()`이면 fallback force 생략 | 에셋에 Root Motion이 있으면 배율 0을 fallback이 보완하지 않는다. |
| `Source/ClassFeature/Private/SWCharacterMovementComponent.cpp:109–158` | 추출된 월드 Root Motion을 피격원 반대 방향으로 재지정 | 프로젝트 고유 방향 처리이며, 배율 0으로 사라진 이동량을 복원하지는 않는다. |
| 같은 파일 `:274–354` | 배에서 낙하 시 보정 억제, 동일 배 Walking에서는 조건부 클라이언트 위치 수용 | 정상 이동 여부와 별개로 오차가 드러나는 시점에 영향을 줄 수 있다. |

설치 엔진 기준 경로:

- `C:/Program Files/Epic Games/UE_5.7/Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/{Public,Private}/Abilities/Tasks/AbilityTask_PlayMontageAndWait.{h,cpp}`
- `C:/Program Files/Epic Games/UE_5.7/Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp`

## 증상과 연결되는 가설

1. 서버가 피격을 확정하고 서버·소유 클라이언트가 피격 몽타주를 시작한다.
2. 서버는 translation scale 0으로 이동량이 제거되고, 소유 클라이언트는 기존 배율로 이동한다.
3. 배 끝에서 클라이언트 캡슐은 갑판을 벗어나 Falling/수영으로 전환하지만 서버 캡슐은 갑판에 남을 수 있다.
4. 위치·MovementMode 불일치에 대한 권위 보정이 도착하면 클라이언트가 배 위로 복귀한다.

배 보정 예외가 실제로 보정을 지연시키는지는 로그로 구분해야 한다. 캡슐이 그대로이고 Mesh만 배 밖으로 나간 경우에는 Root Motion 추출/AnimGraph 설정 문제도 조사한다.

## 권장 구조

`서버 피격 확정 → ServerInitiated 피격 GA → GAS PlayMontageAndWait → AnimMontage Root Motion 추출 → CMC 충돌·바닥·이동 처리 → 내장 이동 복제/보정`

소유 클라이언트는 동일 피격 정의로 이동을 시뮬레이션하고, 비소유 클라이언트는 GAS 몽타주 복제 및 Character의 simulated-proxy Root Motion 처리를 사용한다. CMC의 SavedMove/Root Motion 보정 경로를 유지한다. GAS/CMC가 있다고 임의의 프로젝트 변수까지 자동으로 복제되는 것은 아니다.

Epic 문서는 CMC가 몽타주 Root Motion을 SavedMove에 저장하고, GAS가 몽타주와 Root Motion의 동기화를 지원한다고 설명한다. [Networked Movement](https://dev.epicgames.com/documentation/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine?lang=en-US)

## 단계별 실행 계획

### 1. 최소 재현과 기준 로그 확보

- Listen Server + 원격 소유 클라이언트 + 관찰 클라이언트, Dedicated Server + 2 Clients로 확인한다. Listen Server의 호스트 캐릭터만으로 검증하지 않는다.
- 먼저 평지에서 피격 이동 자체를 확인한 뒤, 정지한 배 끝과 이동·회전 중인 배 끝으로 확장한다.
- 같은 피격을 구분할 식별자와 시간 기준으로 서버/소유 클라이언트의 다음 값을 기록한다: NetRole, 실제 GA 클래스/정책, 몽타주/섹션/재생 위치/속도, AnimInstance 클래스/RootMotionMode, `GetAnimRootMotionTranslationScale()`, 추출 및 적용 translation, 캡슐/mesh 위치, 속도, MovementMode/CustomMode, MovementBase 및 배 기준 상대 위치.
- 보정 발생 시 서버 위치, 클라이언트 위치, 보정 오차와 배 보정 예외 분기 여부를 남긴다. 피격 시작·중단·완료 및 수영 진입도 같은 기록에 연결한다.
- 추출값 확인을 위해 `ConsumeRootMotion()`을 별도로 호출하지 않는다. CMC가 사용할 데이터를 소모하지 않도록 기존 처리 경로에 관찰용 계측을 둔다.

완료 조건: 서버 scale 0/클라이언트 기존 scale 여부, 양측 캡슐 실제 변위, 최초 MovementMode 분기 및 복귀 보정의 발생 시점을 확인한다.

### 2. 공통 피격 몽타주 배율 수정

- `BaseHitReactionGameplayAbility.cpp`의 배율을 `1.0f`로 바꾸고, 시작 시간이 필요하면 별도의 다음 인수로 `0.0f`를 전달한다.
- 인수 의미를 명시하는 주석 또는 이름 있는 상수를 사용한다. 우선 기본 배율 1로 통일한다. ServerInitiated에서 임의 배율을 조절하는 기능은 소유 클라이언트 적용 정책까지 설계하지 않고 추가하지 않는다.
- `ServerInitiated`와 기존 GAS 몽타주 Task를 유지한다. 배율 문제를 우회하려고 LocalPredicted로 변경하지 않는다.
- 같은 공통 부모를 사용하는 적 피격도 함께 확인한다. 기존에 억제되던 적의 Root Motion translation도 살아날 수 있다.
- 이 최소 수정만 적용한 상태에서 1단계 재현을 반복하여 인과관계를 확인한다. 배 보정 정책 변경은 분리한다.

완료 조건: 피격 중 서버/소유 클라이언트의 유효 이동 배율이 1이며, 같은 몽타주 구간에서 충돌 조건에 맞는 서버 변위가 발생한다.

### 3. 실제 에셋 및 서버 애니메이션 평가 확인

- 실제 사용 중인 플레이어 BP 변형별로 부여된 GA 클래스, 앞/뒤/좌/우 몽타주, Skeleton 및 Slot 연결을 조회한다. 파일명이나 기존 테스트 기대값만으로 연결을 확정하지 않는다.
- 몽타주에 포함된 Sequence의 Enable Root Motion, Root Lock, 리타게팅 후 Root Bone 이동, 구간별 추출량을 확인한다. `HasRootMotion()`만으로 유효한 이동량을 보장하지 않는다.
- 실제 AnimInstance/AnimBP의 Root Motion Mode를 `Root Motion from Montages Only` 경로로 맞춘다. 기존 Motion Matching/TIP 처리에 미치는 영향도 확인한다.
- Dedicated Server에서도 AnimInstance가 유효하고, 화면에 보이지 않아도 필요한 몽타주 진행/Root Motion 추출이 수행되는지 확인한다.
- Tick이 원인일 때만 Mesh의 `VisibilityBasedAnimTickOption` 등 설정을 조정한다. CMC의 Root Motion용 Tick 경로를 먼저 확인하며, 모든 캐릭터에 항상 전체 뼈 갱신을 강제하지 않는다. 기존 `AcquireServerCombatPoseRefresh` 참조 카운트 방식과 충돌하지 않게 한다.
- AnimInstance 부재나 몽타주 재생 실패 시 Task 취소/GA 종료/방향 상태 정리를 확인한다. Dedicated Server라는 이유로 null 검사만 통과시켜도 재생이 보장되는 것은 아니다.

완료 조건: 렌더링 유무와 캐릭터 외형에 관계없이 서버가 동일한 피격 Root Motion을 추출·적용한다.

### 4. 커스텀 피격 방향과 재시뮬레이션 검증

현재 `ResolveRootMotionDirection`은 양측의 현재 Actor 위치로 방향을 계산한다. 지연된 클라이언트의 위치/회전으로 계산하면 서버와 방향 또는 선택 몽타주가 달라질 수 있다. `HitReactionRootMotionDirection`과 활성 플래그는 일반 멤버이며, 현재 프로젝트의 SavedMove 확장은 수영 상태만 추가 저장한다. 비소유 클라이언트에서는 GA 실행에 의존한 방향 설정을 기대할 수 없다.

- 배율 수정 후 방향/리플레이 불일치가 남는지 먼저 측정한다.
- 저작된 방향별 몽타주만으로 요구를 충족하면 방향 재지정을 제거하여 표준 몽타주 경로를 단순화하는 선택지를 검토한다. 이 선택은 임의 각도의 피격원 반대 방향 이동을 포기하므로 현재 게임플레이 요구를 유지해야 한다면 선택하지 않는다.
- 임의 방향이 필요하면 서버가 피격 시점에 방향·몽타주 선택을 한 번 확정한다. 기존 GAS 이벤트 payload/TargetData 등 내장 전달 구조를 활용해 소유 클라이언트가 같은 값을 사용하게 설계한다. 이동 중인 피격원을 클라이언트에서 다시 조회하여 방향을 계산하지 않는다.
- 비소유 클라이언트에는 피격 ID, 방향, 시작/종료 등 필요한 최소 상태를 엔진 프로퍼티 복제로 전달하는 경로를 마련한다. 시작/종료 재정렬과 연속 피격의 오래된 상태 적용을 막는다.
- UE 5.7의 CMC 재시뮬레이션 시 Root Motion 재추출/월드 변환 시점을 추적해 커스텀 방향이 다시 사용되는지 확인한다. 필요하면 `FSavedMove_SWCharacter`에 해당 피격의 방향/활성 상태/ID를 저장·복원하고 피격 경계 move 결합을 막는다. SavedMove 추가만으로 서버 전달이 해결되지는 않으므로 서버 확정 상태와 연결한다.
- 정상 종료, 연속 피격 재시작, 회피/공격 중단, 사망, 몽타주 취소에서 방향과 이동 배율이 다음 동작에 남지 않게 검증한다.

완료 조건: 같은 피격에 대해 서버·소유·관찰 클라이언트가 같은 이동 방향을 사용하며, 보정 후 과거 move 재실행에서도 방향이 바뀌지 않는다.

### 5. 배 이탈과 수영 전환의 보정 정책 정리

- CMC의 Walking 바닥 검사와 Falling 전환으로 실제 갑판 이탈을 처리한다. 배 밖으로 밀리게 하려고 Flying으로 바꾸거나 좌표를 직접 이동시키지 않는다.
- 서버와 클라이언트의 배 기준 상대 이동, 바닥 충돌, MovementBase 해제, 배의 선속도/각속도 전달을 비교한다.
- `ServerExceedsAllowablePositionError`의 `LastStandingShip` 조건은 낙하 중 기본 오차 판정을 무조건 무시한다. Root Motion 수정 후에도 필요한 범위를 측정하고, 오래된 배 참조 또는 Walking/Falling 불일치를 숨기지 않도록 제거하거나 제한한다.
- `ClientMovementMode`는 패킹된 값이다. `MOVE_Falling`과 직접 비교하는 부분은 엔진의 `UnpackNetworkMovementMode`를 사용하도록 검토한다. 일반 Walking ground mode에서는 값이 우연히 일치할 수 있어 이 비교만을 현재 버그 원인으로 단정하지 않는다.
- 동일 배 Walking에 대한 15cm 상대 위치 수용은 피격 해결책으로 확대하지 않는다. 피격 및 모드 전환 구간을 별도로 검증한다.
- 프로젝트의 수영은 기본 `MOVE_Swimming`이 아닌 `MOVE_Custom/CMOVE_Swimming`이다. `CheckWaterTransitions`와 `UpdateSwimmingMovement`가 입수 시 잔여 피격 속도/Root Motion을 어떻게 취급하는지 서버·클라이언트에서 확인한다. 입수 후 계속 적용할지 끝낼지 정책을 일치시키고, 커스텀 물리가 Root Motion 속도를 덮어쓰는지 점검한다.

완료 조건: 배 끝에서 실제로 밀려난 서버 캐릭터도 낙하·입수하고, 이후 서버에 근거 없이 배 위로 복귀하지 않는다. 일반 배 보행/점프 안정성도 유지한다.

### 6. 회귀 테스트와 완료 기준

| 구분 | 테스트 |
| --- | --- |
| 환경 | Listen 원격 플레이어, Dedicated, 소유 클라이언트와 관찰 클라이언트 |
| 지형 | 평지, 정지 배, 직진 배, 회전·흔들리는 배, 갑판 모서리, 벽/난간 |
| 피격 | 앞/뒤/좌/우, 이동 중, 연속 피격, 공격/회피 중 피격, 사망 중단 |
| 전환 | Walking → Falling → 커스텀 수영, 낙하 중 재피격, 몽타주 종료 직전 입수 |
| 에셋 | 플레이어 BP 변형, 적 피격, Root Motion 없는 fallback 몽타주 |
| 네트워크 | 지연 없음, RTT 약 100/200ms, jitter 및 1–3% 손실, 서로 다른 프레임률 |

- 기존 `ArtisticSW.GAS.HitReaction.PlayerConfiguration`과 정책 테스트를 실행한다. 기존 테스트는 몽타주 translation을 출력하지만 서버 이동이나 네트워크 동기화를 증명하지 않는다.
- 실제 몽타주 실행을 통해 서버 배율과 변위를 확인하는 의미 있는 회귀 테스트를 추가한다. 정상 구간을 시간 샘플링하여 유효 Root Motion을 확인한다. 최종 순변위 하나만 검사하면 왕복 애니메이션을 잘못 판단할 수 있다.
- 멀티플레이 기능 테스트는 배 끝 피격 후 서버의 갑판 이탈, 모드 전환, 클라이언트 수렴을 확인한다. 네트워크 보정 자체가 0회여야 한다는 기준은 사용하지 않는다.
- 시작 지연을 고려해 동일 몽타주 시점/서버 시간 기준으로 비교한다. 같은 렌더 프레임의 월드 좌표가 완전히 같아야 한다고 요구하지 않는다.
- 초기 수치 목표: 충돌 없는 정지 평지에서 피격 종료·네트워크 안정화 후 캡슐 오차 5cm 이하, 배 끝에서 배 위 복귀 재현 0건. 지연별 안정화 창을 테스트에 명시하고 실제 콜리전/스케일에 맞춰 조정한다.
- 빌드 및 관련 자동화 테스트를 통과한 뒤 네트워크 행렬을 수행한다. 공통 부모 수정으로 적의 이동이 달라지는지도 결과에 포함한다.

## Root Motion Source 대안의 사용 범위

현재 애니메이션의 이동 곡선과 타이밍을 보존하는 요구에는 몽타주 Root Motion 경로를 우선한다. 거리·강도·방향을 게임플레이 파라미터로 직접 제어하는 요구로 바뀌면 GAS의 `ApplyRootMotionConstantForce` 등 내장 Root Motion Source Task가 대안이다. 기존 fallback도 이 방식을 이미 사용한다. RMS는 CMC의 이동 복제 경로를 이용한다. [Epic 설명](https://dev.epicgames.com/documentation/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine?lang=en-US)

대안을 선택할 때도 애니메이션 translation과 RMS를 동시에 이동원으로 쓰지 않으며, ServerInitiated Task의 시작 시점과 소유 클라이언트 실행, 종료 속도, 커스텀 수영 전환을 따로 검증한다. 일정 force로 대체하면 현재 애니메이션의 이동 곡선이 보존되지는 않는다.

## 구현 단위와 우선순위

1. **필수 최소 수정:** 관찰 로그 + 공통 피격 배율 1 + 실제 에셋/서버 평가 검증 + 평지/갑판 재현 비교.
2. **동기화 보강:** 커스텀 방향을 서버 확정값으로 통일하고 필요 시 복제/재시뮬레이션 상태 연결.
3. **배 통합 수정:** 낙하 보정 예외 및 패킹 모드 처리 정리, 입수 시 잔여 피격 처리 검증.
4. **완료 검증:** 관련 자동화 및 다중 프로세스 네트워크 테스트, 플레이어/적 회귀 확인.

최초 조사 시에는 이 계획 문서만 작성했다. 이후 승인된 MVP 구현 결과는 아래에 기록한다.

추가 참고: [Root Motion 설정과 이동 모드](https://dev.epicgames.com/documentation/unreal-engine/root-motion-in-unreal-engine). 웹 문서는 조회 시 5.8로 표시되므로 API 인수와 역할별 동작 판정에는 설치된 UE 5.7 소스를 우선했다.

## MVP 구현 결과 (2026-09-08)

### 적용한 변경

- 공통 피격 GA의 `PlayMontageAndWait` 인수를 이동 배율 `1.0f`, 시작 시간 `0.0f`로 분리하고 의미를 명시했다. 서버에서도 저작된 Root Motion translation이 CMC에 전달된다.
- `LogHitReactionRootMotion`의 Verbose 로그를 추가했다. 몽타주 Task 활성화 직후 Avatar/Role/NetMode, Ability 활성 여부, 몽타주/재생 위치, Root Motion 유무/추출 모드/이동 배율, 캡슐 위치, MovementMode/CustomMode, MovementBase를 확인할 수 있다.
- `AuthorityRootMotion.Man/Woman` 회귀 테스트를 추가했다. 실제 플레이어 Mesh와 설정된 앞/뒤 피격 몽타주를 사용하여 공통 네이티브 GA를 GameplayEvent로 활성화하고, CMC Tick으로 Authority 캡슐 이동을 검사한다. 피격 상태 태그 및 취소 후 정리도 검사한다.
- 기존 `PlayerConfiguration` 테스트의 삭제된 `BP_Player` 경로를 현재 `BP_Player_Man`/`BP_Player_Woman`으로 갱신하고, 각 AnimBP의 `Root Motion from Montages Only` 설정을 검증했다.
- 기존 ServerInitiated/GAS/CMC 경로를 유지한다. 커스텀 방향 복제, 배 보정 예외, 수영 물리 및 에셋 변경은 이번 MVP에 포함하지 않았다. 기존 작업 트리 변경 사항은 유지했다.

### 실행한 검증

- UE 5.7 `ArtisticSW2026Editor Win64 Development` 빌드 성공.
- `ArtisticSW.GAS.HitReaction` 자동화 테스트 **4개 모두 성공**: AuthorityRootMotion.Man, AuthorityRootMotion.Woman, PlayerConfiguration, Policy.
- Authority 테스트에서 남성/여성 Mesh 각각 앞/뒤 몽타주 모두 **0.5초 동안 최대 수평 변위 109.642cm**를 기록했다. 플레이 중 이동 배율 1과 취소 후 State.Damaged 해제를 확인했다.
- 테스트 월드는 렌더링 없는 환경에서 기본 AnimInstance와 공통 GA를 사용한다. 수평 이동만 분리하기 위해 Falling 상태에서 중력을 0으로 둔다. 실제 플레이어 AnimGraph, 배의 충돌/움직임, 네트워크 전송 및 Dedicated Server 전체 실행을 검증한 결과는 아니다.
- 월드 생성 시 발생하는 기존 QuestItem 테이블 오류 3건은 저장소의 기존 월드 테스트와 같이 명시적인 expected error로 분리했다.
- 보고서상 2개는 Success, 2개는 Success with warnings다. 플레이어 에셋을 읽을 때 기존 Foley AnimNotify/실험용 PoseSearch 데이터 누락 경고가 발생했으며, 피격 테스트 실패는 없다.
- 결과: `Saved/HitReactionMVPBuild.log`, `Saved/HitReactionMVPTests.log`, `Saved/HitReactionMVPTests/index.json`.

### PIE에서 이어서 확인할 절차

1. 최신 빌드를 사용하여 Listen Server + 원격 클라이언트를 실행한다. 원격 플레이어로 갑판 끝에서 피격당한다.
2. 서버와 소유 클라이언트에서 `Log LogHitReactionRootMotion Verbose`를 실행한다. 각 피격의 Scale=1, 동일 몽타주, Root Motion 모드와 캡슐/MovementBase를 비교한다.
3. 프레임별 엔진 Root Motion 추출을 볼 때 `Log LogRootMotion Log`, 보정 표시에는 `p.NetShowCorrections 1`, 캡슐 비교에는 `show collision`을 사용한다. 진단 후 로그는 `Log LogHitReactionRootMotion Log`, `Log LogRootMotion Warning`, `p.NetShowCorrections 0`으로 낮춘다.
4. 정지 배 → 이동·회전하는 배 → 네트워크 지연 적용 순으로 반복한다. 서버도 갑판을 벗어나 Falling/수영으로 전환되는지 확인한다.
5. 문제가 남으면 같은 피격의 몽타주/배율이 일치하는지부터 확인한 뒤, 계획의 4·5단계인 방향 재시뮬레이션과 배/수영 보정 처리를 진행한다. 적 피격도 공통 배율 수정의 영향을 받으므로 실제 적의 이동·충돌을 확인한다.
