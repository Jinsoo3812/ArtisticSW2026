# 갑판 일반 적의 검 피격 진단

구현 대조: 2026-10-08. 현재 Editor 대상 C++ 빌드는 성공했다. 아래 BP 설정과 테스트 결과는 2026-10-07 진단 자료이며 이번 문서 점검에서는 전투 자동화를 재실행하지 않았다. 읽기 전용 BP 점검 도구는 `Scripts/Editor/audit_deck_melee_hit.py`이고 결과를 `Saved/DeckMeleeHitAudit.json`에 기록한다. PCG Profile 필터의 구현·레벨별 적용 상태는 [PCG 가이드](PCG_Combat_Fix_Editor_Check.md)를 따른다.

## 현재 확인한 설정

- `BP_DeckMeleeEnemy`, `BP_DeckRangedEnemy` 및 T1 파생 BP의 피격 모드는 `MovementCapsule`이다. 캡슐은 `Pawn`, `QueryAndPhysics`를 사용한다.
- `BP_BaseSwordA`, `BP_BaseSwordB`는 `Pawn` 검색, 단순 충돌 검색, 반경 12cm를 사용한다.
- `WDA_SwordA`, `WDA_SwordB`의 기본 공격 몽타주에는 `ANS_HitScanWindow`가 있으며 서버 실행 옵션이 켜져 있다.
- 따라서 이 설정에서는 일반 적의 Physics Asset 크기를 바꾸는 것이 우선 해결책은 아니다.
- 풀링이 메시 충돌 설정을 덮어쓰는 경로는 별도로 존재하지만, 캡슐 피격 모드인 이 대상의 실패 원인으로 확정할 수 없다.

## 실행 중 판정 확인

빌드 후 에디터를 다시 실행한다. 서버/Listen Server의 PIE 콘솔에서 입력한다.

```text
sw.Combat.Melee.Debug 1
```

`Melee.Debug`는 서버 검의 실제 검색 경로를 잠시 그리며, 공격 창의 시작/종료와 검색된 액터/컴포넌트/본을 기록한다. 2026-10-07 추가 보완 후에는 검의 피해 거절 사유와 체력 변화도 이 명령 하나로 기록한다. 다른 무기까지 공통 피해 로그를 보려면 `sw.Combat.Strength.Debug 1`을 사용한다. 전용 서버에는 화면이 없으므로 로그로 확인한다. 클라이언트 콘솔에서만 켜면 서버 로그와 선이 나오지 않을 수 있다.

초록색 선은 구 검색이 충돌 대상을 찾았다는 뜻이다. 피격 부위/사망/무적/아군/장애물/피해 적용 검사를 통과했다는 의미는 아니다. `Candidate` 이후의 `StrengthHit` 또는 `IgnoredTarget` 기록을 확인한다. 최근 재현 로그에서는 `T1_BP_DeckMeleeEnemy_C_4`의 `CollisionCylinder`와 `CharacterMesh0`가 실제 후보로 검색되었지만, `Strength.Debug`가 꺼져 있어 당시 피해 거절/적용 결과는 기록되지 않았다.

정지한 배와 움직이는 배에서 같은 검으로 같은 일반 적을 공격한다. 서버 화면에서 칼날, 표시된 선, 적 캡슐의 위치를 비교한다. PIE 동안 활성화된 적의 Collision Enabled와 Actor Enable Collision도 확인한다.

| 기록/관찰 | 다음 확인과 수정 |
| --- | --- |
| `WindowOpened`가 없음 | 서버 기본 공격 GA, 몽타주 이벤트 전달, 공격 시작 가드가 실제로 실행되는지 확인한다. |
| `WindowOpened`는 있지만 적 `Candidate`가 없음 | 서버 선이 적 캡슐을 통과하는지 확인한다. 어긋나면 검 부착 소켓, 칼날 끝점, 서버 애니메이션/이동 갱신 순서를 수정한다. 겹쳐도 검색되지 않으면 활성 적의 런타임 충돌 상태를 확인한다. |
| `Rejected=InvalidHitSurface` | 일반 적의 허용 피격 부위는 캡슐이다. 메시 후보만 검색되는지, 캡슐 검색이 누락되는지 확인한다. |
| `Rejected=WorldStaticOcclusion` | 바로 앞의 `Obstruction`에 차단한 액터와 컴포넌트가 기록된다. 갑판/장식의 잘못된 충돌 형상을 수정하고 실제 벽 차단은 유지한다. |
| `Rejected=NoActiveWindow` / `MissingHealthAttribute` / `TargetAvatarMismatch` | 해당 공격 창, 체력 속성, ASC의 Avatar 연결을 확인한다. |
| `Rejected=TargetHealthZero` / `TargetDead` / `TargetInvulnerable` | 체력 및 사망/무적 태그와 풀링 재활성화 시 초기화 상태를 확인한다. |
| `IgnoredTarget` 또는 `Rejected=SameTeam` | 로그의 팀/사망 태그를 확인한다. 검의 사전 필터에 걸렸는지도 기록된다. |
| `Confirmed=1` 및 체력 감소 | 피해 판정은 성공한 것이다. 피격 반응 능력, GameplayCue, 체력 UI를 별도로 확인한다. |

이전 검 위치와 현재 검 위치를 잇는 검색도 월드 좌표를 사용한다. 움직일 때만 실패하고 서버 선이 칼날과 어긋난다면, 애니메이션의 본 갱신 전 샘플링이나 배 이동 갱신 순서를 조사해야 한다. 현재 재현 없이 이 원인으로 단정하지 않는다.

진단이 끝나면 켜 둔 진단 값을 0으로 돌린다.

## 자동 검증 범위

`ArtisticSW.GAS.Strength.AuthoredSwordQueriesDeckCapsule`는 실제 검 A/B 및 T1 갑판 근접 적 BP를 불러와 캡슐을 통과하는 구 검색으로 피해가 발생하고 같은 창에서는 한 번만 적용되는지 확인한다. 대상의 월드 위치를 옮긴 후 새 공격 창도 확인한다.

이 검증은 공격 몽타주, 풀 활성화, 배 물리 이동, 클라이언트와 서버 사이의 위치 오차를 재현하지 않는다. 실제 플레이에서 발생한 실패 원인을 확정하려면 위 진단으로 같은 상황을 다시 확인해야 한다.

2026-10-07 검증: Win64 Development Editor 빌드와 새 `AuthoredSwordQueriesDeckCapsule` 테스트는 통과했다. 함께 실행한 기존 `SharedHitResolver`는 QuestItem 제작 설정 및 `GCNS_Rogue_Arrival` BP 컴파일 오류로 실패했다. 앞서 실행한 기존 `MeleePayload`도 QuestItem 오류와 상태 효과 추가 피해 기대값(15) 대비 실제 피해(10)의 불일치로 실패했다. 전체 전투 테스트 통과를 의미하지 않는다.
