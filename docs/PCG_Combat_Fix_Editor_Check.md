# PCG Volume 충돌 Profile 적용 및 에디터 확인

구현·저장값 대조: 2026-10-08.

## 현재 구현과 적용 상태

`DefaultEngine.ini`에 `PCGVolumeBounds` Collision Profile을 등록했다. Query Only / WorldStatic / Arrow=Block / 나머지 등록 채널=Ignore 설정을 보존하며, PCG 생성 Mesh에 적용하지 않는다.

`UCombatHitResolverComponent`는 WorldStatic Object Multi Trace의 결과 중 이 Profile인 **컴포넌트만** 제외한다. 실제 벽과 다른 Profile의 생성 Mesh는 계속 검사한다. PCG 클래스·Brush 타입·모든 응답 Ignore 검사는 사용하지 않으며 GASCore에 PCG 모듈 의존성도 없다.

이는 WorldStatic 장애물 검사를 사용하는 플레이어/적 근접·보스 Resolver 호출에 적용된다. 화살 호출은 이 추가 검사를 끄고 기존 투사체 충돌을 사용한다. Profile 이름 필터만으로 PCG의 화살 Block 응답은 바뀌지 않는다.

2026-10-08 두 저장 레벨을 다시 읽은 **현재 상태**:

| 레벨 | PCG_Biome Brush Profile | Collision Enabled | 식생 인스턴스 | 적용 상태 |
| --- | --- | --- | ---: | --- |
| LV_ET | PCGVolumeBounds | Query Only | 3,886 | 적용됨 |
| Lvl_CY | Custom | Query Only | 3,886 | 추가 적용 필요 |

2026-10-07의 `Saved/PCG_Profile_Migration/report.json`에는 두 레벨 적용 결과가 기록되어 있지만, 현재 Lvl_CY 디스크에는 Custom이 저장되어 있다. 과거 보고서만으로 현재 적용 완료를 판단하지 않는다. 이번 문서 점검은 레벨을 변경하지 않았다.

## Lvl_CY 적용 절차

`Scripts/Editor/apply_pcg_volume_bounds_profile.py`는 LV_ET와 Lvl_CY를 대상으로 기존 설정이 Profile과 정확히 일치할 때만 Brush Profile을 지정한다. 백업·저장·재조회 보고서는 Saved/PCG_Profile_Migration에 둔다. 이전 백업이 있으면 덮어쓰지 않으므로 재적용 전 백업 날짜와 현재 레벨 버전을 확인한다.

에디터에서 저장되지 않은 변경을 정리한 뒤 실행하고, 보고서와 실제 저장 레벨의 Profile을 함께 확인한다. PCG Generate/Cleanup과 생성 Mesh의 충돌은 변경하지 않는다. 수동 적용이라면 PCG_Biome의 BrushComponent0만 선택하고 PCGVolumeBounds를 지정한 뒤 저장·재열기로 확인한다. 실제 벽·생성 Mesh에는 이 Profile을 지정하지 않는다.

## PIE에서 확인

1. 빌드 후 에디터를 다시 열고 LV_ET의 PCG_Biome → BrushComponent0에서 Profile=PCGVolumeBounds, Query Only, WorldStatic, Arrow=Block과 나머지 Ignore를 확인한다.
2. 1인 PIE의 서버 콘솔에서 `sw.Combat.Melee.Debug 1`을 입력한다.
3. 기존 Tag에 맞춰 이동한다. 예: `SW.EnemyShip.Teleport DebugShip01_1 DebugArrival`.
4. PCG 영역 안에서 일반 적을 공격한다. Brush가 조회되면 `StrengthHit IgnoredCollisionProfile=PCGVolumeBounds`가 기록되어야 한다. 체력 감소와 Confirmed=1로 피해 성공을 판단한다.
5. 검 사거리 안에서 플레이어와 적 사이에 Static / BlockAll / WorldStatic Cube를 놓는다. 후보가 장애물 검사에 도달하면 Cube와 Rejected=WorldStaticOcclusion이 기록되고 벽 뒤 피해가 없어야 한다.
6. Cube를 제거하고 Players=2 / Listen Server에서 호스트·클라이언트 피해를 확인한다.
7. Lvl_CY는 Profile 적용·저장 확인 후 같은 절차를 수행한다.
8. `sw.Combat.Melee.Debug 0`으로 진단을 끈다.

## 검증과 자료

2026-10-08 Editor 대상 C++ 빌드는 성공했다. 저장된 Profile과 인스턴스 수를 읽기 전용으로 확인했다. 실제 PIE 피해·벽 차단, PCG 재생성 및 패키지 실행은 아직 확인하지 않았다. 이번 점검에서 전투 자동화 테스트를 추가하거나 재실행하지 않았다.

- `Saved/CommitPreparation/assets.json`, `assets-verified.log`: 최신 저장 레벨 조회. 기존 GCNS_Rogue_Arrival BP 컴파일 오류도 로딩 로그에 기록됨.
- `Saved/PCG_Profile_Before.json`: 이전 등록 채널별 응답 조사.
- `Saved/PCG_Profile_Migration/report.json`: 2026-10-07 적용 보고서. 현재 레벨과 재대조 필요.
- `Saved/PCG_Profile_Migration/LV_ET.before.umap`, `Lvl_CY.before.umap`: 이전 백업. LV_ET는 이번에 새로 Git에 포함하는 레벨이다.
- [영향 검토](PCG_Biome_Collision_Impact_Review_2026-10-07.md), [검 피격 진단](Deck_Melee_Hit_Diagnosis.md).

Saved의 진단/백업은 Git 제외 경로이며 이번 푸시에 포함하지 않는다.
