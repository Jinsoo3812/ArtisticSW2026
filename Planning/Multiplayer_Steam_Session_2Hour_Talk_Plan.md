# 멀티플레이 게임 2시간 발표 계획

## 목표와 범위

- 대상: Unreal 멀티플레이 입문자. C++의 클래스, 함수, Blueprint 호출을 알고 있다고 가정한다.
- 발표 후 수강생은 서버 권한, Actor 복제와 RPC의 역할을 구분하고, Steam 세션을 검색해 강사의 listen server에 접속할 수 있다.
- 실습의 초점은 이미 패키징한 클라이언트로 **방 찾기 → 입장 → 같은 게임 상태 관찰**이다. Steam API를 강의 중 처음부터 타이핑하지 않는다.
- 코드 설명은 기존 `OnlineSubsystem`/`IOnlineSession` 경로로 통일한다. `Online Services`의 새 Sessions Interface와 혼용하지 않는다.

## 현재 프로젝트에서 출발할 지점

현재 `USWRoomSubsystem`은 방 코드를 공인 IPv4와 포트로 변환하고, 에디터의 별도 전용 서버 프로세스를 실행한다. `USWConnectionSubsystem`은 그 주소에 `ClientTravel`하며 연결·맵 이동 실패를 delegate로 받는다. 이것은 Steam 검색 기반 listen server 예제와 다른 구조이므로 **새 실습용 경로**를 준비한다. 기존 방 코드 흐름은 마지막에 비교 사례로 짧게 보여준다.

게임 사례로는 `AShip`의 `UNetworkPhysicsComponent` + Resimulation, `PlayerArrowProjectile`의 서버 생성·적중, 플레이어 Character Movement를 사용한다. 물리 예측이 프로젝트에 설정되어 있어도 실제 수업용 패키지의 동작은 별도 검증해야 한다.

## 120분 진행표

| 시간 | 분 | 내용 | 보여줄 장면/확인 질문 |
|---|---:|---|---|
| 00:00–00:10 | 10 | 서버–클라이언트 구조, listen server와 dedicated server, 권한 | 강사 PC가 서버이면서 플레이어인 구조 그리기 |
| 00:10–00:23 | 13 | 서버 권한, Replication, RPC, GameMode/GameState/PlayerState | 클라이언트에서만 체력을 바꾸면 왜 다른 화면에 안 보이는가? |
| 00:23–00:35 | 12 | `UGameInstanceSubsystem`과 delegate | 비동기 세션 작업의 시작 반환값과 완료 callback 구분 |
| 00:35–00:52 | 17 | Steam OnlineSubsystem 세션 생성·검색·참가 흐름 | Steam Lobby, 게임 내 대기 화면, 게임 서버를 각각 표시 |
| 00:52–01:12 | 20 | 수강생 빌드 실행·검색·강사 방 입장 | 검색 결과의 강사 방 선택, 참가 인원과 서버 공유 상태 확인 |
| 01:12–01:17 | 5 | 휴식·접속 문제 정리 | 실패자에게 사전 준비한 대체 영상/로컬 데모 제공 |
| 01:17–01:32 | 15 | 지연과 네트워크 보정의 문제 | 0 ms/100 ms/손실 조건에서 움직임 비교 |
| 01:32–01:47 | 15 | 게임 사례: 캐릭터, 선박, 화살 | 소유자 예측/서버 보정, 선박 재시뮬레이션, 서버 적중 구분 |
| 01:47–01:57 | 10 | 지연 주입 라이브 데모와 해석 | 배 조타 및 활 발사 후 화면·서버 차이 관찰 |
| 01:57–02:00 | 3 | 세 가지 핵심 정리·질문 | 세션은 발견, 서버는 판정, 보정은 체감 품질 |

질문이 많은 반이라면 01:32–01:47의 선박 내부 구조 설명을 5분 줄여 질의응답에 쓴다. 참가 인원이 많으면 20분 실습이 부족할 수 있으므로 조교가 설치·로그인 문제를 사전에 처리한다.

## 설명 순서와 핵심 도식

1. **누가 진실을 결정하는가:** Host의 listen server가 게임 규칙과 최종 상태를 결정한다. 수강생 클라이언트는 입력/요청을 보내고 결과를 받는다. `GameMode`는 서버 전용, `GameState`는 공유 상태, `PlayerState`는 플레이어별 공유 상태라는 작은 예제로 시작한다.
2. **세션은 접속 흐름의 안내자:** Steam에 세션/매치메이킹 로비가 등록되면 클라이언트가 이를 검색할 수 있다. Steam이 게임 시뮬레이션의 권한 서버가 되는 것은 아니다. 게임 내 `Lobby` 맵/위젯은 Steam Lobby와 별개다. 세션 검색 결과로 참가한 뒤 실제 접속 문자열을 얻어 `ClientTravel`한다.
3. **Subsystem을 쓰는 이유:** 맵이 바뀌어도 생성·검색·참가 작업의 상태와 delegate를 관리할 객체가 필요하다. `UGameInstanceSubsystem`의 `Initialize`/`Deinitialize`와 수명을 설명한다. 이 프로젝트의 `USWConnectionSubsystem`이 실제 예다.
4. **Delegate를 쓰는 이유:** `CreateSession()` 등의 즉시 반환은 요청 시작 여부만 알려준다. 성공 여부는 `OnCreateSessionComplete` 같은 완료 delegate에서 확인한다. 등록 시 handle 보관, 중복 클릭 방지, 완료/해제 시 handle 제거, 객체 소멸과 늦은 callback을 짚는다.

```text
Host UI → SessionSubsystem.Create → IOnlineSession.CreateSession
        → OnCreateSessionComplete → Lobby 맵 listen으로 열기
Client UI → SessionSubsystem.Find → OnFindSessionsComplete
          → 강사 방 선택 → JoinSession → OnJoinSessionComplete
          → GetResolvedConnectString → ClientTravel
이후: Client 입력 → Server RPC/이동 경로 → 서버 판정 → 복제 → Client 표시
```

## 배포 예제의 최소 사양

- 버튼: `방 만들기`(강사), `새로고침`, 검색 결과 목록, `입장`, `나가기`.
- 검색 카드: 방 이름, 지도/모드, 현재/최대 인원, 강사 식별용 수업 코드, 빌드 버전. 수업 코드와 버전을 검색 결과의 사용자 정의 메타데이터로 표시·필터링한다. 수강생이 첫 번째 결과를 무조건 자동 참가하게 하지 않는다.
- 상태: Idle → Creating/Finding/Joining → Traveling/InLobby → Failed. 진행 중 버튼 잠금, 타임아웃/실패 안내, 중복 요청 차단.
- 세션: 온라인 검색 가능, LAN 아님, 최대 인원 설정, presence/lobby 사용. Steam Lobby 경로에서는 `bUsesPresence`와 `bUseLobbiesIfAvailable`을 켠다. Create/Find/Join/Destroy 완료 delegate를 각 작업에 연결한다.
- Host 성공 후 게임 내 대기 맵을 `listen`으로 열고, 입장한 수강생은 같은 대기 맵에서 명단 또는 공유 상태를 본다. 시작 버튼은 서버만 처리하고 이후 플레이 맵으로 서버 이동한다.
- 참가 실패, 호스트 종료, 인원 초과, 버전 불일치, 세션 정리 후 재검색을 확인한다.

## 강의 전 검증과 배포 운영

1. 기존 방 코드·공인 IP·별도 전용 서버 경로와 실습용 Steam listen server 경로를 분리하고, 배포용 빌드가 의도한 경로를 기본으로 여는지 확인한다. 현재 코드에는 Steam OSS 의존성/설정과 세션 구현이 없으므로 이 작업이 선행되어야 한다.
2. 사용 엔진 버전(프로젝트는 UE 5.7), 플러그인, Steam NetDriver, App ID, 패키지 구성을 고정한다. 시험용 App ID 480은 공유 테스트용이다. 수강생 배포에는 자체 App ID와 접근 권한/키 또는 Steam Playtest를 권장한다.
3. 서로 다른 PC와 Steam 계정으로 패키징된 **동일 빌드**를 시험한다. 먼저 2인, 그다음 예상 인원 규모로 검색·참가·맵 이동·재입장·호스트 종료를 시험한다. Steam 실행/로그인, 방화벽, Steam 초기화 로그, 검색 필터를 점검한다.
4. 동시 인원과 강사 PC의 성능·업로드 대역폭을 측정한다. listen server는 강사 PC가 시뮬레이션을 맡으므로 전원이 한 방에 접속하는 데 한계가 있다. 예상 정원이 불안정하면 조교 방을 여러 개 열거나 2인 단위 실습으로 전환한다.
5. 수강생에게 실행 파일, 압축 해제/실행법, Steam 로그인과 계정 접근 조건, 강의용 방 이름·수업 코드, 재접속 방법을 사전 배포한다. 발표 당일에 설치 시간을 쓰지 않는다.
6. 네트워크 장애용으로 2 PC 성공 영상과 로컬 데모를 준비한다. 라이브 실습 실패가 개념 설명 전체를 막지 않도록 한다.

## 네트워크 보정 파트의 깊이

- **기본 문제(3분):** RTT, 지터, 손실 때문에 서버 상태가 늦게 도착한다. 화면상의 즉시 반응과 서버의 단일 판정을 함께 달성해야 한다.
- **캐릭터(4분):** Autonomous Proxy의 로컬 입력 예측 → 서버 검증 → 차이가 크면 보정 및 저장된 이동 재생. 다른 사람이 보는 Simulated Proxy에는 보간/스무딩이 중요하다.
- **배(5분):** `AShip`은 `UNetworkPhysicsComponent`와 Resimulation을 설정하고 입력·상태 이력을 이용한다. 서버 상태가 과거 프레임의 예측과 다르면 되감아 다시 계산한다. 파도·충돌·조타가 있는 물리 객체에서 비용과 보정 튐을 함께 설명한다. 이 프로젝트의 설정에서 물리 예측은 활성이고 Iris는 비활성이다.
- **활(3분):** 클라이언트의 조준/발사 요청, 서버의 유효성 검증과 화살 생성·적중을 보여준다. 이동 중인 배의 소켓 점속도를 초기 화살 속도에 더한다. 이 구현은 서버 시점의 배 물리 상태로 발사를 확정하므로 지연 시 조준 화면과 서버 판정의 차이가 남을 수 있다. 이것을 완전한 hit rewind 사례로 소개하지 않는다.
- **데모(10분):** 정상 조건과 약 100 ms 지연 조건을 비교한다. 조타 반응, 원격 배 위치, 화살 1발당 피해 1회, 호스트/클라이언트 화면의 차이를 기록한다. 패킷 손실은 리허설을 통과하면 짧게만 보여준다.

심화 주제인 lag compensation/서버 시점 되감기 적중 판정, adaptive dead reckoning, Iris 정책 변경은 마지막 한 장의 확장 과제로 둔다. 현재 저장소의 adaptive dead reckoning 문서는 **제안/계획**이지 시연 가능한 완료 기능으로 소개하지 않는다.

## 리허설 완료 기준

- 두 Steam 계정의 패키지 빌드에서 강사 방이 검색되고, 선택한 방에 들어가 공유 상태가 보인다.
- 강사가 방을 닫거나 게임을 종료하면 수강생에게 실패/종료가 표시되고 재검색할 수 있다.
- 수강생 수만큼 접속했을 때 프레임·패킷 손실·입장 성공률이 수업 진행에 적합하다.
- 지연 데모에서 정상/지연 차이를 객관적으로 보여줄 로그 또는 화면 녹화가 있다.

## 참고 자료

- Epic: [Networking Overview](https://dev.epicgames.com/documentation/unreal-engine/networking-overview-for-unreal-engine)
- Epic: [Online Subsystem Session Interface](https://dev.epicgames.com/documentation/unreal-engine/online-subsystem-session-interface-in-unreal-engine)
- Epic: [Online Subsystem Steam](https://dev.epicgames.com/documentation/unreal-engine/online-subsystem-steam-interface-in-unreal-engine)
- Epic: [Programming Subsystems](https://dev.epicgames.com/documentation/unreal-engine/programming-subsystems-in-unreal-engine)
- Epic: [Character Movement Networking](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine)
- Epic: [Networked Physics Overview](https://dev.epicgames.com/documentation/unreal-engine/networked-physics-overview)
- Valve: [Steam Matchmaking & Lobbies](https://partner.steamgames.com/doc/features/multiplayer/matchmaking)
- Valve: [Testing on Steam](https://partner.steamgames.com/doc/store/testing)
