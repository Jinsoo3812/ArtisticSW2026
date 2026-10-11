# 배 위 플레이어의 균일한 점프 구현 계획

작성일: 2026-10-11 (Asia/Seoul). 상태: 설계안. 게임 코드 및 에셋 변경 없음.

## 1. 목표와 권장 정책

같은 출발 상태와 입력이면 배의 전진 속도·가속·회전에 관계없이 같은 점프 조작 결과를 제공한다. 네트워크 환경에서도 이 규칙을 예측하고 재생할 수 있어야 한다.

‘독립적’이라는 요구에는 서로 다른 두 결과가 있다.

| 방식 | 규칙 | 플레이 결과 |
| --- | --- | --- |
| WorldIndependent | 배의 선속도·각속도를 상속하지 않고 월드에서 점프 | 월드 궤적은 일정하지만 배가 아래에서 이동하므로 갑판 착지 지점은 달라짐 |
| ShipRelative (권장) | 배 기준으로 점프를 계산하고 배의 이동은 좌표 변환으로만 적용 | 제자리 점프는 배 위 같은 상대 위치로 돌아오고 방향 점프의 상대 거리는 일정함 |

권장안의 ‘독립’은 배의 운동량이 플레이어의 점프 속도에 섞이지 않는다는 뜻이다. 배의 좌표계는 따라간다. 
실제 월드 이동에는 배 이동이 포함된다. 월드 궤적의 독립과 배 기준 착지 위치의 고정을 동시에 보장할 수는 없다.

WorldIndependent를 비교용 소규모 단계로 먼저 검증하고, 갑판 플레이에 적합한 ShipRelative를 최종 구현하는 순서다. 
최종 정책이 WorldIndependent로 결정되면 비교 단계와 해당 네트워크 검증에서 범위를 마칠 수 있다.

## 2. 현재 구현에서 확인한 사실

- 프로젝트 엔진 연결은 UE 5.7이다. 실제 구현 기준 API는 설치된 UE 5.7 소스로 확인한다.
- ABasePlayer는 Jump() / StopJumping()을 사용한다. 점프 알림 RPC는 애니메이션 이벤트 경로로 유지하고 이동 판정을 맡기지 않는다.
- ABasePlayer 생성자에서 bBaseOnAttachmentRoot=true, bImpartBaseVelocityZ=false를 설정한다. X/Y와 각속도 상속을 배 점프에 한정해서 차단하는 코드는 확인되지 않았다. Blueprint의 실제 덮어쓰기 값은 구현 전 확인한다.
- UE 5.7 GetImpartedMovementBaseVelocity()는 활성화된 축의 배 속도에 각속도에 의한 접선 속도를 포함할 수 있다. ApplyImpartedMovementBaseVelocity()가 이를 플레이어 Velocity에 더한다.
- DoJump(bool, float)는 기존 수직 속도와 JumpZVelocity 중 큰 값을 사용한다. 따라서 외부 상승 속도가 이미 들어온 상태에서는 고정 초기 속도가 보장되지 않는다.
- 배는 Network Physics Resimulation을 사용한다. 물리 설정은 60Hz이고, custom state 비교용 위치 임계치는 Ship.cpp에서 최대 5cm로 제한한다.
- 플레이어의 FSavedMove_SWCharacter 및 NetworkMoveData 확장은 수영 예측 상태와 파도 시간을 처리한다. 점프용 배 프레임·상대 상태 확장은 없다.
- Walking에서는 같은 배·같은 이동 모드와 허용 상대 오차(기본 15cm)를 조건으로 클라이언트 상대 위치를 수용하는 코드가 있다.
- LastStandingShip이 남아 있고 서버 또는 클라이언트가 Falling이면 위치 오차 보정을 무조건 생략하는 분기가 있다. 새 점프 검증을 추가할 때 이 분기를 교체해야 한다. 보정이 보이지 않는 것만으로 동기화 성공이라고 판단하지 않는다.

실제 증상의 단일 원인을 확정한 상태는 아니다. 런타임 속도·기반·프레임 기록으로 관성 상속, 외부 이동, 충돌, 시간축 오차를 분리한다.

## 3. 게임플레이 사양

### 3.1 공통 규칙

- 배에서 실제 점프가 승인되는 첫 이동 단계에만 정책을 시작한다. 입력 이벤트 순간에 확정하지 않는다.
- 플레이어의 걷기/달리기에서 나온 상대 수평 속도는 유지한다. 배의 선속도·접선 속도는 점프 추진력에 가산하지 않는다.
- 첫 점프의 수직 초기 속도는 설정값으로 고정한다. 피격·Launch·루트 모션이 진행 중인 경우에는 해당 능력 정책이 우선한다.
- 균일성의 비교 조건은 출발 속도, 입력 방향, 입력 길이, 캐릭터 설정이 동일한 경우다. 걷기와 달리기의 점프 거리가 같다는 의미는 아니다.
- 고정 높이를 기본으로 한다면 JumpMaxHoldTime=0을 적용한다. 길게 누르는 점프를 유지하려면 동일한 누름 시간끼리 비교하고 별도 사양으로 기록한다.
- 착지 위치를 미리 지정하거나 입력 전체를 강제로 고정하지 않는다. 공중 조작과 실제 충돌은 계속 적용한다.
- 육지와 다른 이동 플랫폼에는 기존 규칙을 유지한다. 정책은 유효한 AShip에서 출발한 승인된 점프에 한정한다.

### 3.2 ShipRelative 규칙

- 점프 진행, 중력, 공중 가속도는 배 로컬 좌표에서 계산한다.
- 배 로컬 +Z를 점프 위쪽으로 사용한다. 배가 기울어지면 점프 방향도 기울어진다. 이 비현실적인 규칙은 갑판 기준 일관성을 위한 의도된 선택이다.
- 이동 입력은 카메라 입력 방향을 배 평면에 투영하고 정규화한다. 배가 회전 중일 때도 입력 정책을 서버·클라이언트에서 같은 방식으로 계산한다.
- 플레이어 캡슐 및 시각적인 몸의 기울기는 별도 정책으로 둔다. 초기안은 월드 기준 직립을 유지한다. 이 경우 큰 롤/피치에서는 캡슐의 상대 공간 여유가 변하므로 협소한 장소의 충돌까지 완전히 동일해지지는 않는다.
- 갑판/난간/천장 접촉은 실제 충돌이 우선한다. 장애물로 궤적이 달라진 경우는 균일성 실패와 구분한다.
- 배를 따라가는 이동에 별도의 기반 속도 상속이나 기반 Transform delta를 중복 적용하지 않는다.
- 배 로컬 높이·거리는 일정하게 만들 수 있지만 월드 높이·거리 및 외부 물체와의 충돌 결과는 배 자세에 따라 달라진다.

### 3.3 이탈과 종료

| 상황 | 처리 |
| --- | --- |
| 출발 배의 보행 가능한 면에 착지 | Walking, 실제 접촉 기반 재설정, 점프 상태 종료 |
| 다른 배/육지에 착지 | 실제 착지 대상 기반으로 전환, 출발 배 참조 해제 |
| 배 옆으로 뛰어나감 | 로컬 탑승 범위와 지지면 규칙에 따라 World Falling으로 1회 전환 |
| 바다 진입 | 기존 수영 진입 조건을 확인하고 점프 상태 해제 |
| 배 파괴·참조 해제·큰 텔레포트 | 현재 유효한 월드 위치에서 안전하게 Falling으로 전환 |
| 피격/Launch/강제 이동 | 능력이 점프를 중단하고 자신의 이동 정책을 적용 |
| 사망·리스폰 | 점프 참조·이력·보간 상태 정리, 기존 사망 정책 적용 |
| 입력 없이 난간에서 떨어짐 | MVP는 기존 Falling 유지; 자발적 점프 정책과 구분 |

탑승 범위는 배 로컬 공간에서 정의한다. 상층/하층을 포함한 보행면과 점프 높이 여유, 측면 이탈 여유를 둔다. 월드 AABB나 배와의 단순 거리만으로 결정하지 않는다. 경계에서는 작은 히스테리시스를 사용하고 수치는 재현 시험 후 확정한다.

World Falling으로 전환할 때 기본값은 플레이어의 상대 속도를 월드 방향으로 회전시킨 값이며 배 속도는 가산하지 않는다. 위치는 유지한다. 이탈 순간 월드 속도가 불연속적일 수 있다는 게임플레이 비용은 시험한다. 배 운동량을 남기는 선택지는 별도 옵션이며 기본 목표와 구분한다.

## 4. 1단계: 관성 상속 제거 비교 구현

목적: 수평/회전 속도 상속이 얼마나 큰 원인인지 최소 범위에서 검증한다.

1. USWCharacterMovementComponent의 GetImpartedMovementBaseVelocity() 또는 ApplyImpartedMovementBaseVelocity() 확장 경로에서 승인된 배 점프에 한정해 전달 속도를 0으로 만든다.
2. UE 5.7 DoJump(bool bReplayingMoves, float DeltaTime) 경로에서 첫 점프 및 출발 배를 확정한다. Super 호출로 기반이 해제되기 전에 출발 정보를 보관한다.
3. 점프 초기 수직 속도를 고정하고, FormerBaseVelocity 및 루트 모션에 배 속도가 다시 들어오는 경로를 확인한다. Super를 호출하고 나서 단순히 배 속도를 빼는 방식은 중복 제거와 각속도 오차를 만들 수 있으므로 피한다.
4. 입력 없이 낙하, 타 플랫폼, 이중 점프, hold 재호출을 구분한다. 전체 캐릭터의 상속 플래그를 영구적으로 끄는 것보다 정책 범위를 명시적으로 제한한다.
5. 정지/전진/후진/회전 중 같은 입력의 월드 궤적을 비교한다.

예상 결과: 배 속도 때문에 발사되는 현상은 줄고 월드 점프는 균일해진다. 전진하는 배가 캐릭터 아래에서 빠져나가는 현상은 남는다. 이 결과를 갑판 점프의 최종 해결로 오인하지 않는다.

## 5. 2단계: 배 기준 점프 이동

### 5.1 상태와 인터페이스

예정 타입 FShipJumpPredictionState:

- 활성 여부, 정책 식별자, 점프 시퀀스.
- 출발 Ship 및 기준 PhysicsRoot 식별자.
- 시작 시뮬레이션 프레임, 현재 이동의 기준 프레임/샘플 정보.
- 배 로컬 위치와 플레이어 상대 속도.
- 시작 수평 속도, 경과 시간, 이탈/종료 상태.
- 재생 경계와 히스토리 가용 여부.

입력, 계산된 상태, 렌더 보간용 상태는 분리한다. 클라이언트가 보내는 계산 결과를 서버의 정답으로 취급하지 않는다.

예정 설정은 ShipJumpPolicy, JumpInitialSpeed, JumpGravity, ShipAirControl, ShipAirMaxSpeed, JumpHoldPolicy, SupportedJumpTilt, 탑승 범위, 상대 검증 허용 오차다. 기존 CMC 설정을 재사용할 수 있는 부분은 재사용하고 중복 튜닝 값을 최소화한다.

### 5.2 이동 모드 선택

권장 구조는 CMOVE_ShipJump와 PhysCustom 내 전용 경로다. 기존 CMOVE_Swimming=1은 보존하고 새 값을 추가한다.

이유: 배 로컬 속도를 CMC의 월드 Falling Velocity와 혼용하지 않고, 기반 이동의 중복 적용을 차단하며, 상태 전환을 명확하게 하기 위해서다. 다만 custom mode만 추가하면 기본 Falling 기능을 모두 상속하는 것은 아니다. 중력 적분, 공중 조작, 천장/벽 처리, 착지, 잔여 시간 처리를 명시적으로 구현한다.

새 모드에서 IsFalling()은 기본적으로 true가 아니므로 다음 통합을 함께 수정한다.

- AnimState 및 이동 스냅샷의 공중/점프/착지 판정.
- ACharacter의 점프 count, hold, CanJump, landed/reset 흐름.
- SwimmingComponent의 수면 전환 조건.
- GAS 이동 제한·피격·루트 모션·죽음의 공중 판정.
- 낙하 피해 또는 공중 상태를 검사하는 기존 기능.

구현 전 이 호출부 목록을 검색해 확인한다. CMC Falling 유지 대안은 이 통합 비용이 큰 경우에 비교하되, 로컬 적분/월드 충돌/네트워크 재생을 동시에 일관되게 처리할 수 있음을 먼저 검증한다.

### 5.3 이동 계산

배 프레임 Transform을 T, 배 로컬 위치를 p, 플레이어 상대 속도를 u로 둔다.

    u_next = Integrate(u, input, local_gravity, dt)
    p_desired = Integrate(p, u, dt)
    world_desired = T_sample.TransformPosition(p_desired)

배 선속도와 각속도는 u에 더하지 않는다. 배 이동은 T_sample로만 반영한다. 적분은 CMC의 시간 분할 원칙과 일치시키고 프레임별 dt 차이에 의한 오차를 제한한다.

실제 이동은 이전 유효 월드 위치에서 world_desired까지 swept movement로 수행한다. 충돌 후 실제 위치를 기준 프레임의 로컬 좌표로 되돌려 p를 갱신한다. 벽에서 slide, 천장에서 상승 속도 제거, 보행 가능한 착지 면과 하강 조건 검사, 착지 후 남은 시간 처리를 구현한다.

충돌에 영향을 주는 배 Transform이 계산에 사용한 Transform과 달라서는 안 된다. 네트워크 재생 중에는 단순히 현재 배 충돌에 sweep하고 과거 로컬 위치와 비교하면 시간축이 다시 어긋난다. 아래 프레임/충돌 설계가 필수 의존성이다.

## 6. 3단계: 네트워크 예측과 배 프레임 연결

### 6.1 하나의 이동 프로토콜 사용

기존 FSavedMove_SWCharacter와 FCharacterNetworkMoveData_SWCharacter를 확장한다. 점프 이동용 별도 Tick RPC/Multicast는 추가하지 않는다. 수영의 Custom_0/Custom_1 플래그는 보존한다.

- SetMoveFor: 점프 정책, 출발 배, 시퀀스, 기준 프레임과 재생에 필요한 시작 상태를 저장.
- PrepMoveFor: 해당 이동의 점프 상태를 복구하되, 서버 correction이 승인한 시작 상태를 오래된 로컬 상태로 덮어쓰지 않도록 재생 순서를 설계.
- Clear: 배 참조와 상태 완전 초기화.
- CanCombineWith: 시작/종료/배/정책/프레임 경계에서 잘못된 이동 병합 방지.
- ClientFillNetworkMoveData/Serialize: 서버에 필요한 요청 정보만 전송. 배 참조는 PackageMap을 통한 유효한 네트워크 식별자로 처리.
- MoveAutonomous: 서버 검증 및 해당 프레임을 사용한 동일 이동 실행.

Jump 버튼은 기존 CMC 입력으로 유지한다. 정책과 출발 배는 서버가 실제 지지면·입력·출발 이력으로 승인한다. 양측에서 같은 프레임에 지지면을 잃은 경계 상황은 최근 유효 지지 이력과 제한된 시간 창으로 처리한다.

### 6.2 기준 프레임과 충돌 히스토리

AShip/ShipPhysicsAsync 쪽에 제한된 Transform history 조회 인터페이스를 마련한다. Network Physics의 ServerFrame과 CMC ClientTimeStamp는 다른 값이므로 직접 동일시하지 않는다.

1. CMC 이동 시작/끝 시간을 배의 물리 프레임에 매핑하는 규칙을 정의한다.
2. 완료된 물리 상태를 스레드 안전하게 GT로 전달하고 프레임 번호와 함께 기록한다.
3. 서버 및 클라이언트 재생이 같은 논리 프레임을 조회한다. 중간 시점 보간 규칙도 동일하게 한다.
4. 배 rewind 후 영향받은 캐릭터 saved moves가 이전 배 history를 계속 사용하는지 검사하고 무효화/재생 경계를 연결한다.
5. history가 없거나 배 참조가 unresolved인 경우 제한된 보류 또는 서버 correction으로 수렴시킨다. 임의의 현재 Transform을 ‘같은 과거 프레임’으로 사용하지 않는다.
6. 잘못된 프레임, 오래된 점프, 순서 역전, 시간 스탬프 reset, late join을 처리한다.

충돌 설계는 먼저 작은 프로토타입으로 검증한다. 배 자체 충돌은 로컬 공간의 안정된 충돌 표현 또는 별도 질의 프록시를 사용해 해당 프레임과 일치시킨다. 과거 판정을 위해 실제 공유 배 Actor를 앞뒤로 옮기는 방식은 사용하지 않는다.

외부의 다른 배·육지·물체는 별도 시간축 정책이 필요하다. MVP에서는 현재 서버의 권위 있는 외부 충돌을 우선하고, 전환 순간 correction을 허용한다. 움직이는 외부 대상까지 동일 재생을 요구하면 해당 대상의 history가 추가 범위다. 월드 직립 캡슐을 배 로컬 질의로 변환할 때 회전/형상도 변환해야 하며, 위치만 변환한 upright capsule 질의로 대체하지 않는다.

### 6.3 서버 검증과 correction

현재 LastStandingShip + Falling 무조건 correction 생략 분기를 제거하거나 새 정책 범위로 대체한다.

- 서버는 실제 출발 가능 상태, 점프 횟수, 속도 한계, 시퀀스, 프레임 범위, 배 식별자를 검증한다.
- 같은 배 점프에서는 같은 기준 프레임의 로컬 위치·상대 속도·점프 상태를 비교한다.
- 위치뿐 아니라 배 이탈, 착지, 이동 모드 불일치도 correction 대상이다.
- 작은 수치 오차에는 제한된 허용 범위를 적용한다. 상대 좌표라도 벽 통과나 과도한 속도를 허용하지 않는다.
- correction 응답에 필요한 배/시퀀스/프레임/상대 상태를 담는다. 기본 CMC 응답만으로 복구가 가능한지 확인하고, 부족하면 move response data 경로를 확장한다.
- owning client는 승인된 상태를 적용하고 미확인 이동을 재생한다. 배 rollback과 캐릭터 correction이 중복으로 변위를 적용하지 않게 한다.
- 기존 Walking 15cm 수용 정책은 별도다. 점프에 같은 값을 자동 복사하지 않고 실측한 오차로 결정한다.

## 7. 원격 표현과 카메라

- simulated proxy에는 활성 배·점프 상태 및 필요한 상대 표현 상태를 전달한다. 소유 클라이언트의 saved moves와 역할을 구분한다.
- 배의 렌더 보간과 탑승 캐릭터 표현이 같은 기준 시점을 참조하도록 한다. gameplay/collision Transform과 visual Transform을 구분한다.
- capsule correction과 mesh/camera 보간을 분리한다. 화면이 부드러워도 서버 상대 오차가 커지면 실패다.
- 난간/천장 등 협소 구간에서 보간 때문에 관통해 보이는지 확인한다.
- 포즈의 roll/pitch 및 카메라 수평 유지 정책은 이동 규칙과 별도로 검토한다.

## 8. 변경 예정 파일

| 파일/영역 | 변경 책임 |
| --- | --- |
| Source/ClassFeature/Public 및 Private/SWCharacterMovementComponent.* | 점프 정책, 전용 이동, 예측/응답, 검증, 상태 전환 |
| Source/ClassFeature/Private/BasePlayer.cpp | 입력 연결 유지, 애니메이션 이벤트를 실제 점프 승인과 정합화, 설정 |
| Source/ClassFeature/Public/SwimmingComponent.h 및 관련 구현 | 새 custom mode 값과 수영 전환의 통합 |
| Source/ClassFeature/Private/Animation 및 이동 상태 소비부 | 새 공중 상태의 애니메이션/GAS 판정 |
| Source/WaterAndShip/Public 및 Private/Ship*, ShipPhysicsAsync.* | 완료된 물리 프레임 history 및 조회/rewind 통지 |
| Source/ClassFeature/Private/Tests/ 신규 점프 테스트 | 정책 수학, 직렬화, 재생, 종료/검증 테스트 |
| 플레이어/배 Blueprint 및 진단 레벨 | 실제 덮어쓰기 확인, 튜닝 및 재현 환경 |

가능하면 점프 상태 타입과 로컬 적분 계산은 작은 전용 파일로 분리한다. 엔진 수정 및 Mover 전환은 초기 범위에 포함하지 않는다.

## 9. 검증 행렬과 완료 기준

### 시험 조건

- 배: 정지, 일정 전진/후진, 가속/감속, 급회전, 파도 roll/pitch, 배 correction 발생.
- 플레이어: 제자리, 전후좌우 걷기/달리기 점프, 방향 전환, 난간, 천장, 배 밖 이탈, 다른 배/육지 착지, 바다 진입, 피격/사망.
- 실행: standalone, listen server의 호스트와 원격 플레이어, dedicated server, 최소 2클라이언트.
- 네트워크: 목표 RTT 0/50/100/200ms, 손실 0/1/3%, 지터 조합. 실제 RTT를 측정하고 도구의 편도 지연 설정과 구분한다.
- 성능: 30/60/120 FPS 및 불규칙 frame time, 60Hz 물리, 다중 배/다중 탑승자.

### 계측

점프마다 출발 배·시퀀스·프레임, 상대 초기 속도, 배 선속도/각속도, 상대 최고 높이, 체공 시간, 상대 착지 변위, 이동 모드 전환, correction 횟수/크기, 최종 서버 오차를 남긴다. 기존 correction 억제 여부도 기록한다.

### 제안 수용 기준

장애물 없는 동일 입력과 평평한 동일 갑판 조건에서:

- ShipRelative의 상대 최고 높이와 이동 거리는 정지 배 기준 대비 2% 이내. 제자리 점프는 거리 비율 대신 상대 착지 오차 2cm 이내를 목표로 한다.
- 체공 시간은 기준 대비 한 번의 시뮬레이션 적분 단계 또는 2% 중 큰 범위 이내를 목표로 한다.
- 연속 정상 점프에서 매 이동마다 correction이 발생하거나 눈에 보이는 반복 스냅이 없어야 한다. 횟수뿐 아니라 상대 위치/속도 오차가 유한하게 수렴해야 한다.
- 출발/착지/이탈/수영/사망 전환에 배 참조가 남거나 점프가 다시 시작되지 않아야 한다.
- 큰 오류·불법 입력·history 결손 시 correction이 작동해야 한다.
- 월드 장애물, 큰 배 기울기, 다른 배 착지의 결과는 별도로 평가한다. 위 수치를 그대로 보장하지 않는다.

수치는 구현 완료 사실이 아니라 초기 제안 목표다. baseline 측정 후 프로젝트의 허용 화면 오차와 성능 예산으로 확정한다.

## 10. 실행 순서와 단계 종료 조건

1. Baseline: 런타임 Blueprint 설정 및 재현 로그 수집. 관성 상속/충돌/시간축 원인을 분리.
2. WorldIndependent 비교: 배 점프 관성 전달 차단 및 고정 초기 속도. 배가 아래에서 이동하는 결과까지 검증.
3. ShipRelative 오프라인: 로컬 적분, 공중 조작, swept collision, 착지/이탈/수영. standalone 완료.
4. 프레임/충돌 프로토타입: history 매핑과 배 rewind 뒤 재생 일관성 입증. 불가능하면 충돌·시간축 설계를 조정한 뒤 진행.
5. 네트워크: saved moves/packed data/response와 서버 검증. 무조건 correction 생략 분기 교체.
6. 표현: 원격 캐릭터, mesh/camera 및 배 렌더 보간 정합화.
7. 회귀 및 부하: 검증 행렬 실행, 결과 기록, 임계치와 설정 확정.

각 단계는 기능과 검증 결과를 함께 남긴다. 정책 플래그로 기존 동작과 비교하고 문제가 있는 단계는 이전 검증 상태로 복귀할 수 있게 한다. 새 이동 모드는 점프·애니메이션·네트워크가 모두 준비된 시점에 기본 활성화한다.

## 11. Network Physics와 Net Update Frequency의 역할

Network Physics는 배 자체의 예측과 correction에 계속 사용한다. 캐릭터의 점프 규칙은 CharacterMovement에 구현한다. 업데이트 빈도 증가나 correction 임계치 확대는 배 속도 상속을 제거하지 못한다.

전송 빈도와 물리 주기는 분리해서 측정한다. 최종 튜닝은 움직임 규칙과 프레임 재생이 안정된 뒤 진행하고 실제 상태 전송률·대역폭·CPU를 비교한다. 배를 Predictive Interpolation으로 바꾸는 실험은 조종 응답과 충돌에 영향을 줄 수 있으므로 별도 작업으로 둔다.

## 12. 근거

- 현재 저장소: BasePlayer.cpp, SWCharacterMovementComponent.cpp/.h, Ship.cpp/.h, ShipPhysicsAsync.cpp, Config/DefaultEngine.ini.
- 설치된 UE 5.7 CharacterMovementComponent.cpp/.h: DoJump(bool,float), GetImpartedMovementBaseVelocity, ApplyImpartedMovementBaseVelocity 및 correction 흐름.
- [Epic: CharacterMovement 네트워크 이동 및 saved move 확장](https://dev.epicgames.com/documentation/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine)
- [Epic: Networked Physics Overview, UE 5.7](https://dev.epicgames.com/documentation/en-us/unreal-engine/networked-physics-overview?application_version=5.7)
- [Epic: Get Imparted Movement Base Velocity](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Pawn/Components/CharacterMovement/GetImpartedMovementBaseVelocity)

온라인 문서는 최신 버전으로 표시될 수 있다. 실제 API/실행 순서는 위에 명시한 로컬 UE 5.7 소스를 기준으로 한다.
