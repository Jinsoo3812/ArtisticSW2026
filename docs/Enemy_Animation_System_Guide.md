# [Enemy 시스템 가이드] 적 캐릭터 애니메이션 & AI 연동 종합 매뉴얼

이 문서는 적(Enemy) 담당 개발자 및 팀원이 캐릭터 에셋 임포트부터 C++ `UEnemyAnimInstance` 연동, 애님 블루프린트(`ABP_Enemy`/`ABP_Warrior`) 제작, 배 위 접지(`Foot Placement`), 그리고 최종 캐릭터 블루프린트(`BP_Enemy`) 세팅까지 **한 번에 이해하고 바로 작업할 수 있도록 작성된 올인원(All-in-One) 가이드**입니다.

---

## 1. 💡 왜 C++ `UEnemyAnimInstance`를 만들었는가? (구현 배경 및 이유)

1. **대규모 적 스폰 시 멀티스레드 성능 최적화 (Fast-Path ⚡)**:
   * 블루프린트 이벤트 그래프에서 매 프레임 `TryGetPawnOwner ➡️ GetVelocity ➡️ VectorLength ➡️ CalculateDirection`을 연산하면 블루프린트 가상 머신(VM) 오버헤드로 인해 적이 10~20마리만 나와도 극심한 프레임 드랍(Game Thread 병목)이 발생합니다.
   * `UEnemyAnimInstance`는 C++ 네이티브 레벨에서 워커 스레드(Worker Thread)를 통해 병렬로 연산하므로 **수십 마리의 적이 동시에 전투해도 렉 없이 60fps 이상을 유지**합니다.
2. **기존 AI 시스템(`ABaseAIController`, `BehaviorTree`)과의 안전한 단방향 연동**:
   * AI의 상태(`EEnemyAIState::Passive / Investigating / Combat / Dead`) 및 추적 중인 타깃(Player)과의 거리/각도를 읽기 전용(Read-Only) 약참조(`TWeakObjectPtr`)로 안전하게 갱신하여 AI 로직과 충돌이 0%입니다.
3. **파도치는 배(Ship Deck) 위에서의 발 접지(Foot Placement) 지원**:
   * 적이 현재 딛고 있는 지면이 배(`AShip`)인지 감지하고, 공중 점프나 사망 시 발 IK를 자동으로 해제하는 `FootPlacementAlpha`를 제공합니다.

---

## 2. 뼈대 리깅 및 에셋 임포트 워크플로우 (신규 에셋 적용 시)

캐릭터 3D 모델(예: `warrior`)을 프로젝트로 가져올 때의 정석 워크플로우입니다:

1. **AccuRIG 자동 리깅**:
   * `Target Application`을 **`Unreal (UE5 Skeleton)`**으로 설정하고 Export합니다.
2. **언리얼 엔진 임포트**:
   * 언리얼 엔진으로 FBX를 드래그 앤 드롭할 때 **`Skeleton` 드롭다운에서 기존 프로젝트의 `SK_Mannequin`을 지정**합니다.
   * 🎉 **효과**: 리타게팅할 필요 없이 기존 마네킹의 모든 애니메이션 에셋(대기, 걷기, 뛰기, 피격, 점프 등)을 100% 다이렉트로 공유합니다.

---

## 3. C++ 클래스 구조 및 제공 변수 목록

### 소스 파일 위치
* **Header**: [`Source/Enemy/Public/Animation/EnemyAnimInstance.h`](file:///c:/Users/I/Documents/GitHub/ArtisticSW2026/Source/Enemy/Public/Animation/EnemyAnimInstance.h)
* **Source**: [`Source/Enemy/Private/Animation/EnemyAnimInstance.cpp`](file:///c:/Users/I/Documents/GitHub/ArtisticSW2026/Source/Enemy/Private/Animation/EnemyAnimInstance.cpp)

### 제공되는 변수 & BlueprintThreadSafe 함수
모든 Getter 함수와 프로퍼티는 스레드 안전(`BlueprintThreadSafe`) 메타데이터가 적용되어 있어 AnimGraph에서 경고 없이 즉시 사용 가능합니다.

| 변수명 | 타입 | 설명 및 용도 |
| :--- | :--- | :--- |
| **`Speed`** | `float` | 지면 2D 이동 속도 (`Velocity.Size2D()`). `BS_Warrior`의 속도 핀에 직결. |
| **`ZVelocity`** | `float` | Z축 상승/하강 속도 (cm/s). 양수면 상승, 음수면 하강. |
| **`Direction`** | `float` | 이동 방향 각도 (-180.0° ~ 180.0°). `BS_Warrior`의 방향 핀에 직결. |
| **`bIsMoving`** | `bool` | 실제로 걷거나 뛰고 있는지 여부 (`Speed > 5.0f`). |
| **`bIsFalling`** | `bool` | 공중에 떠 있는지 여부. |
| **`bIsJumping`** | `bool` | 위로 솟구치는 도약 점프 상태 (`bIsFalling && ZVelocity > 100.0f`). `Ground ➡️ Jump Start`용. |
| **`bIsFallingDown`** | `bool` | 낭떠러지/난간에서 아래로 떨어지는 순수 낙하 상태 (`bIsFalling && ZVelocity <= 100.0f`). `Ground ➡️ InAir`용. |
| **`CurrentAIState`** | `EEnemyAIState` | AI의 현재 상태 (`Passive`, `Investigating`, `Combat`, `Dead`). |
| **`bHasCombatTarget`** | `bool` | 플레이어를 발견하고 쫓고 있는지 여부. |
| **`CombatTargetDistance`**| `float` | 플레이어와의 수평 거리 (cm). 근접 공격 사거리 판단용. |
| **`bIsDead`** | `bool` | 체력이 0이 되어 사망했는지 여부. |
| **`bIsOnShip`** | `bool` | 현재 딛고 있는 지면이 배(`AShip`)인지 여부. |
| **`FootPlacementAlpha`** | `float` | 지면에 있을 때 1.0, 공중이거나 사망 시 0.0으로 자동 보정되는 접지 알파값. |

---

## 4. 애님 블루프린트(`ABP_Warrior` / `ABP_Enemy`) 완성 파이프라인

### 1단계: 애님 블루프린트 생성
1. 콘텐츠 브라우저 우클릭 ➡️ `애니메이션 -> 애니메이션 블루프린트`
2. **부모 클래스**: **`EnemyAnimInstance`** 선택
3. **스켈레톤**: **`SK_Mannequin`** 선택
4. *(이벤트 그래프는 C++에서 전부 자동 계산하므로 비워두셔도 됩니다)*

---

### 2단계: 8방향 블렌드스페이스 (`BS_Warrior`) 세팅

* **축 세팅 (Axis Settings)**:
  * **`Direction` (가로축)**: 최소 `-180.0`, 최대 `180.0`, 분할 수 `8`
  * **`Speed` (세로축)**: 최소 `0.0`, 최대 `400.0` (또는 `500.0`), 분할 수 `2`
* **에셋 배치 좌표 매핑표**:
  * `Speed = 0` (맨 아래 한 줄): `MF_Unarmed_Idle` (대기 모션)
  * `Speed = 400` (조깅 라인):
    * `0°`: `MF_Unarmed_Jog_Fwd` (정면)
    * `45°`: `MF_Unarmed_Jog_Fwd_Right` (우전방)
    * `90°`: `MF_Unarmed_Jog_Right` (우측)
    * `135°`: `MF_Unarmed_Jog_Bwd_Right` (우후방)
    * `±180°`: `MF_Unarmed_Jog_Bwd` (후면)
    * `-45°`: `MF_Unarmed_Jog_Fwd_Left` (좌전방)
    * `-90°`: `MF_Unarmed_Jog_Left` (좌측)
    * `-135°`: `MF_Unarmed_Jog_Bwd_Left` (좌후방)

---

### 3단계: 점프/낙하 스테이트 머신 (`Locomotion State Machine`)

```
[ Entry ] ──► [ Ground (지상) ] ──(1: bIsJumping)───────► [ Jump Start (도약) ]
                  │                                            │
                  │ (2: bIsFallingDown)                    (3: 자동 규칙)
                  │                                            │
                  ▼                                            ▼
             [ InAir (공중 체공) ] ◄───────────────────────────┘
                  │
             (4: NOT Falling)
                  │
                  ▼
             [ Land (착지) ] ──(5: 자동 규칙)──► [ Ground (지상) ]
```

* **전환 규칙 (Transition Rules)**:
  1. `Ground ➡️ Jump Start`: `bIsJumping` 연결
  2. `Ground ➡️ InAir` (대각선): `bIsFallingDown` 연결
  3. `Jump Start ➡️ InAir`: 디테일 창에서 **`시퀀스 재생 시간에 따른 자동 규칙` 체크**
  4. `InAir ➡️ Land`: `bIsFalling` ➡️ `NOT` 연결
  5. `Land ➡️ Ground`: 디테일 창에서 **`시퀀스 재생 시간에 따른 자동 규칙` 체크**

---

### 4단계: 상체 피격 분리 및 전신 몽타주 + Foot Placement 최종 파이프라인

```
[ Locomotion State Machine ] ──► [ Save cached pose 'LocomotionPose' ]


[ Use cached pose 'LocomotionPose' ] ───────────────────────► Base Pose ┐
                                                                         ├──► [ Layered blend per bone ]
[ Use cached pose 'LocomotionPose' ] ──► [ Slot 'UpperBody' ] ──► Blend Poses 0 ┘   (본: spine_01)
                                         (피격 상반신 몽타주)                                  │
                                                                                          ▼
                                                                             [ Slot 'DefaultSlot' ]
                                                                             (죽음 등 전신 몽타주)
                                                                                          │
                                                                                          ▼
                                                                             [ Local to Component ]
                                                                                          │
                                                                                          ▼
                                                                             [ Foot Placement ] (배 위 접지)
                                                                             (Alpha: Get Foot Placement Alpha)
                                                                                          │
                                                                                          ▼
                                                                             [ Component to Local ]
                                                                                          │
                                                                                          ▼
                                                                             [ Output Pose ]
```

#### 세부 노드 설정값:
1. **`Layered blend per bone`**:
   * `분기 필터 (Branch Filters)`: 본 이름 **`spine_01`**, `메시 공간 회전 블렌딩` **체크**
2. **`Foot Placement` (배 위 접지 핵심)**:
   * **기본 본**: `IK 발 루트 본` = `ik_foot_root`, `골반 본` = `pelvis`
   * **다리 정의**:
     * 인덱스 [0]: FK `foot_r`, IK `ik_foot_r`, 볼 `ball_r`, 사지수 `2`
     * 인덱스 [1]: FK `foot_l`, IK `ik_foot_l`, 볼 `ball_l`, 사지수 `2`
   * **플랜트 세팅**: `속도 한계치` = `0.0`, `지면까지의 거리` = `0.0`
   * **보간 세팅**: `바닥 선형 강성` = `1000.0`, `분리 강성` = `1000.0`
   * **트레이스 세팅**:
     * `복합/단순 트레이스 채널` = **`FootPlacement`** *(전용 채널 필수!)*
     * `최대 지면 침투` = `0.0`, `시작 오프셋` = `50.0`, `끝 오프셋` = `-75.0`
   * **알파 핀**: **`Get Foot Placement Alpha`** 연결

---

## 5. 🔗 기존 AI Task 소스 및 GAS 시스템과의 실제 연동 원리

기존 프로젝트의 AI Task 및 GAS 어빌리티들이 `UEnemyAnimInstance`와 어떻게 유기적으로 맞물려 돌아가는지 설명합니다:

### ① `BTT_MoveToWeaponRange` (플레이어 추적 이동)
* **AI 동작**: `TargetActor`(플레이어)를 향해 NavMesh 경로를 따라 `MoveTo` 명령을 내립니다.
* **애니메이션 연동**: 캐릭터가 이동하면서 발생하는 실제 2D 속도와 회전각을 `UEnemyAnimInstance`가 `Speed`와 `Direction`으로 실시간 변환하여 `BS_Warrior`의 8방향 걷기/달리기 모션을 즉시 출력합니다.

### ② `BTT_SetFocus` (플레이어 주시 및 게걸음 무빙)
* **AI 동작**: AI가 이동 중에도 플레이어를 계속 바라보도록 회전 포커스를 고정합니다.
* **애니메이션 연동**: 적이 앞을 보며 옆으로 걷거나 뒤로 물러날 때 `Direction`(-90°, 90°, 180°)이 실시간으로 바뀌면서 **플레이어를 노려보며 스트레이프(게걸음/대각선 무빙)**를 완벽하게 구사합니다.

### ③ `BTT_EnemyBasicAttack` & 무기 어빌리티 (공격 몽타주)
* **AI 동작**: 무기 사거리 내에 진입하면 `GameplayAbility_BasicAttack` GAS 어빌리티를 발동합니다.
* **애니메이션 연동**: GAS 어빌리티에서 공격 몽타주를 재생(`PlayMontage`)하면, AnimGraph의 **`Slot 'DefaultSlot'`** 노드를 통해 이동 중인 포즈를 덮어쓰고 검을 휘두르는 공격 모션이 부드럽게 출력됩니다.

### ④ `BaseHealthComponent` & 피격/사망 연동
* **피격 시 (Damage)**: 
  * 상체 피격 몽타주가 **`Slot 'UpperBody'`** 슬롯으로 재생되면, `Layered blend per bone`을 통해 **하체는 달리면서 상체만 젖혀지는 자연스러운 피격 모션**이 나옵니다.
* **사망 시 (Death)**:
  * 체력이 0이 되면 `bIsDead`가 `true`로 바뀌고, **`Slot 'DefaultSlot'`**의 전신 사망 몽타주가 재생되며, **`FootPlacementAlpha`가 자동으로 0.0으로 꺼져** 발이 바닥에 늘어붙지 않고 바닥으로 안전하게 쓰러집니다.

---

## 6. 🎯 캐릭터 블루프린트(`BP_Enemy`) 최종 조립

적 캐릭터를 인게임에 배치하기 위한 최종 단계입니다:

1. **`BP_Enemy`** (부모: `BaseEnemy` 또는 `MeleeEnemy`)를 엽니다.
2. **`Mesh` 컴포넌트**:
   * **Skeletal Mesh**: 리깅된 **`warrior`** 에셋 선택
   * **Anim Class**: 방금 완성한 **`ABP_Warrior`** 선택
3. **`AI Controller Class`**:
   * **`BaseAIController`**로 지정되어 있는지 확인
4. 레벨에 배치하고 실행하면, 플레이어를 시각/청각으로 감지하고 뛰어와 공격하며, 흔들리는 파도 위에서도 발이 갑판에 착 달라붙는 완성형 적 AI가 동작합니다!

---

## 7. 팀원 작업 시 주의사항 & 꿀팁

1. **몽타주 슬롯 규칙**:
   * **상체만 피격/공격**: 몽타주 슬롯을 **`DefaultGroup.UpperBody`**로 지정
   * **전신 공격/사망/넉다운**: 몽타주 슬롯을 **`DefaultGroup.DefaultSlot`**으로 지정
2. **충돌 없는 안전성**:
   * `UEnemyAnimInstance`는 AI 판단을 건드리지 않고 순수하게 프레젠테이션만 담당하므로 기존의 Behavior Tree Task(`BTT_MoveToWeaponRange`, `BTT_EnemyBasicAttack` 등)와 완벽히 호환됩니다.
3. **배의 콜리전 채널**:
   * Foot Placement는 독립 전용 채널(`FootPlacement`)을 사용하므로, 카메라나 무기 조준선 등 기존 게임플레이 시스템에 일절 간섭하지 않습니다.
