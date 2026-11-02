# 총잡이 발사 원통·총구 Niagara와 갑판 적 Focus

대상 보스: `/Game/GameplayAbilitySystem/Enemy/Balancing/T1/BP_T1Boss_Enemy`.

최신 동작: 초기 조준 중에는 숨기고 **발사 0.5초 전부터 공격 완료까지** 주황 원통을 표시한다. 발사 뒤에는 공격의 후딜까지 유지하고, 공격 능력이 정상 완료되면 원통도 즉시 숨기고 제거한다. 1초 고정 만료와 완료 후 남는 표시는 사용하지 않는다. 공격 취소·사망 때도 즉시 제거한다. 시작점은 매 프레임 현재 `Gun_Muzzle`, 끝점은 현재 조준 충돌점 또는 발사 당시 충돌한 대상/함선의 로컬 지점이며, 배와 대상의 이동·회전을 따라간다.

공격 완료 동기화 변경 검증(2026-10-31): `Saved/GunnerAttackEndBeamBuild.log` 빌드 성공, `Saved/GunnerAttackEndBeamTestsReport/index.json` 자동 검사 3개 성공. 원통 검사는 긴 공격 중 1초 뒤에도 유지, 1초 이전 정상 완료 시 즉시 숨김/파괴, 취소 시 즉시 제거, 이동한 양 끝점 연결과 끝점 파괴 시 숨김을 포함한다. 기존 주기 소환과 타깃 유지 검사도 통과했다. 실제 PIE/멀티플레이 화면 검증은 미실행이다.

별도 확인: 이번 작업 전 08:53에 저장된 총잡이 BT에는 Task가 4개이며 Focus 서비스 전체 검사가 실패한다. 이전의 3개 Task/20초 Sequence 구조를 엄격히 요구하는 소환 에셋 검사도 실패한다(`Saved/GunnerAttackEndBeamAudit.log`, `Saved/GunnerAttackEndBeamVisualAudit.log`). 이 작업에서는 해당 BT 에셋을 보존했다. 이번 발사 표시 변경은 C++ 종료 처리와 그 검사에 적용된다.

## 직접 조절할 위치

보스의 Combat Data로 연결된 `T1_DA_GunnerCombat`을 연다.

| 설정 | 초기값 | 의미 |
|---|---|---|
| Ray → Shot Radius | 4cm | 실제 원통 반경. 화면 픽셀이 아닌 월드 단위 |
| Ray → Shot Color | 주황 | 발사 원통 색 |
| Ray → Shot Brightness | 6 | 발광 세기 |
| Ray → Pre Shot Show Time | 0.5초 | 예정 발사 시각보다 이 시간만큼 앞서 표시 |
| Ray → Shot Lifetime | 이전 값 보관 | 호환용 값. 현재 종료 시점은 공격 완료가 결정 |
| Ray → Shot Material | M_T1Boss_ShotCylinder | 수정/교체 가능한 Additive·Unlit 재질 |
| Presentation → Muzzle Flash | NS_T1Boss_MuzzleFlash | 발사마다 생성할 총구 Niagara |
| Presentation → Muzzle Flash Scale | 0.35 | 총구 효과 전체 크기 |

발사 전 표시는 서버의 예정 발사 시각으로 계산한다. Lock Time과는 별개이므로 조준 고정 0.35초보다 앞선 0.5초 전부터 표시할 수 있다. 실제 발사 후에는 같은 공격이 끝날 때까지 연결을 유지한다. 현재 보스는 기본 단발만 사용하며, 단발 후딜 0.35초가 끝나 공격이 완료될 때 원통도 함께 종료한다.

총구는 매 프레임 다시 조회한다. 끝점은 충돌 컴포넌트/본의 로컬 좌표를 저장해 대상의 이동·회전을 따라가며, 허공을 쏘면 함선 로컬 좌표를 기준으로 유지한다. 클라이언트도 각 화면의 현재 총구와 끝점 프레임을 사용한다.

원통은 충돌이 없는 시각 효과다. 피해 판정은 발사 순간의 기존 서버 Line Trace 1회이며, 이후 원통 갱신으로 피해를 추가 적용하지 않는다. `Thickness`, `Shot Thickness`, 준비 색상, `Shot Lifetime`, `End Presentation Lifetime`은 이전 데이터 호환용이다. 반경·색·밝기·발사 전 표시 시간은 계속 조절할 수 있으며, 발사 후 유지 시간은 공격의 Recovery Time을 따른다.

## 빠른 발사 후 복귀

단발 후딜을 1.2초에서 0.35초, 연사 마지막 후딜을 2.5초에서 0.5초로 변경했다. 단발의 후딜 후 공통 공격 대기는 0.6초에서 0.2초다. 실제 준비·고정·피해·발 수·쿨다운은 유지한다.

전용 단발/연사 몽타주의 Fire 세그먼트는 0.2초 이내에 반동 클립 전체를 재생하고 Recover 준비 자세로 넘어가도록 했다. 긴 Recover 루프를 유지하던 기존 후딜도 단축해, 빠르게 이동/다음 공격 판단으로 복귀한다. 원본 Rifle 클립, 총구 Niagara 크기, 원통 반경/색/밝기는 수정하지 않았다.

설정 적용 스크립트: `Scripts/tune_gunner_beam_and_recovery.py`. 변경 전 데이터/몽타주: `Saved/GunnerRecoveryTimingBackup`. 재로드 검사: `Scripts/audit_gunner_beam_window_and_recovery.py`.

표시 시간/빠른 복귀 변경 검증: `Saved/GunnerBeamWindowBuild.log` 빌드 성공, `Saved/GunnerBeamWindowAudit.log`에서 저장된 0.5초/1초 및 단발 0.35초/연사 0.5초 확인, `Saved/GunnerBeamWindowTestsReport/index.json` 자동 검사 2개 성공. 발사 전 경계, 발사 후 종료 경계, 연사 구간 겹침, 다음 긴 조준 중 숨김, 짧은 후딜 후 자동 제거 예약을 검사했다. 실제 PIE 화면 검증은 미실행이다.

재질 및 Niagara 경로: `/Game/GameplayAbilitySystem/Enemy/Balancing/T1/VFX`.

- `M_T1Boss_ShotCylinder`: 실제 Cylinder 메시에서 사용하는 발광 재질. `ShotColor`, `Brightness`, `Fade` 파라미터를 제공한다.
- `NS_T1Boss_MuzzleFlash`: 기존 `P_MuzzleFlash_01`을 전용 복제하고 MuzzleFlash01 이미터만 활성화한 간단한 섬광. 원본 효과는 보존했다. Niagara 에디터에서 수명·색·크기·스프라이트를 추가 조절할 수 있다.

Niagara는 서버 확정 사격에서만 클라이언트/호스트에 1회 생성한다. 전용 서버에서는 시각 효과를 만들지 않는다. 일회성 네트워크 연출은 패킷 손실 시 생략될 수 있지만 피해에는 영향이 없다.

## 갑판 적과 보스의 시선 유지

9개 Combat Subtree의 118개 Task에 기존 `Maintain Deck Combat Focus` 서비스를 각각 연결했다. 단발 SetFocus Task만 앞에 추가하는 대신 Task 진입 및 실행 중 0.1초 간격으로 같은 Focus 동작을 수행한다. 기존 그래프의 순서·레이아웃·Decorator·공격 선택은 보존한다. Controller Tick도 Task 전환 사이의 Focus를 복구한다.

적용 대상: 갑판 근접/원거리 Combat·NoAlarm 트리, Rogue·Samurai·기존 T1/T3 보스 트리, 총잡이 전용 트리.

- 처음 인식해 획득한 타깃만 유지한다. 보이지 않는 플레이어를 새로 선택하지 않는다.
- 일시적인 시야 상실이나 난간 가림 때 같은 갑판의 유효 추적 앵커가 있으면 현재 플레이어를 버리지 않는다.
- 공격 중 지연된 타깃 재평가에서도 같은 규칙을 사용한다.
- 시야를 잃은 뒤에는 주기적으로 추적 가능 여부를 검사하므로, 사망·파괴·갑판 이탈 시 영구 추적하지 않는다. 기존 짧은 추적 유예는 유지한다.
- 기존 ClearFocus Task가 전투 종료만으로 플레이어 시선을 끊지 않도록 했다. 일반 지상 적의 ClearFocus는 기존대로다.
- 총잡이 공격 중에는 공격 컴포넌트가 추적/고정 방향을 소유한다. Focus 서비스가 고정된 사선을 플레이어 쪽으로 다시 돌리지 않는다. 돌진·은신·스턴·넉백 방향도 강제로 덮지 않는다.

## 재현 및 검증

- 생성/연결: Editor 빌드 후 `Scripts/author_gunner_shot_and_deck_focus.py`.
- 저장 재로드 검사: `Scripts/audit_gunner_shot_and_deck_focus.py`.
- 자동 검사 묶음: `ArtisticSW.Enemy.ShotFocus`.
- 변경 전 Combat Data와 BT 복사본: `Saved/GunnerShotFocusBackup`.

실제 화면에서 원통 반경·밝기 및 총구 소켓 정렬을 확인하고 취향에 맞게 조절한다. PIE/멀티플레이 시각 검증은 별도 확인 대상이다.

2026-10-31 검증 결과:

- Editor Win64 Development 빌드 성공: `Saved/GunnerShotFocusFinalBuild.log`.
- 새 에디터 프로세스에서 원통 재질·Niagara 참조 및 9개 트리의 118개 Task 서비스 재로드 확인: `Saved/GunnerShotFocusAudit.log`, `GUNNER_SHOT_FOCUS_RELOAD_AUDIT_OK`.
- `ArtisticSW.Enemy.ShotFocus` 자동 검사 2개 통과: `Saved/GunnerShotFocusTestsReport/index.json`.
- ShotCylinder: 아래 후속 변경 전에는 발사 전용 표시와 시간 만료를 검사했다. 최신 검사는 조준 시작 표시, 이동한 총구/대상에 대한 연결, 공격 종료 시 즉시 제거를 검사하도록 변경했다.
- SightLossAndTaskRefresh: 일반 갑판 적과 총잡이 보스 각각에서 같은 갑판의 시야 상실 후 타깃 유지, Task 사이 Focus 복구, 갑판 이탈 시 타깃 해제 확인.

연속 연결 후속 검증도 통과했다: `Saved/GunnerLiveBeamBuild.log`, `Saved/GunnerLiveBeamTestsReport/index.json`(2개 성공). 새 ShotCylinder 검사는 조준 시작 표시, 이동한 현재 총구, 이동·회전한 대상의 로컬 충돌점, 두 끝점에 따른 원통 위치/길이 갱신, 후딜 중 유지, 끝점 파괴 시 숨김, 공격 완료 시 액터 즉시 제거를 확인한다. 2인 PIE 화면 검증은 미실행이다.
