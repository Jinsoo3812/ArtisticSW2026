# Lvl_WJLast 보스 조우 테스트 배치

2026-10-31 변경.

- 기존 레벨과 변경한 BP의 원본: `Saved/WJLast_before_edit/`.
- 레벨에 배치된 EnemyShip은 BP_ES_Mid_1, BP_ES_Mid_3, BP_ES_Final 3척만 유지. 나머지 45척 제거.
- 플레이어 함선의 기존 해상 위치 유지, 서쪽으로 향하도록 회전. 두 플레이어 시작점을 갑판에 배치.
- 플레이어 함선 기준 서쪽 80m / 350m / 600m에 세 적선을 배치.
- 각 함선의 기존 보스 클래스와 BossSpawnPoint ID 20 유지.
- Encounter Enabled=true, Player Ship Sight, Required Story Node=GameStarted.
- 각 보스의 Stop After Story Node는 MiddleBoss1Defeated / MiddleBoss3Defeated / FinalBossDefeated.
- 독립적인 WJLast 편대 ID 사용. Final의 캠페인 퀘스트 전용 함선 등장 제한을 이 테스트 레벨에서 우회한다. 기존 캠페인의 Final 편대 완료 처리 대신 보스 조우 컴포넌트의 완료 노드를 사용한다.
- BP_DeckRangedEnemy 및 T1~T4 기본값에서 공격 LOS/추적/경로 Debug=false 확인. BP 기본값이므로 이 클래스를 사용하는 다른 레벨에도 적용된다. Projectile.DebugLaunch는 현재 에디터 세션에서 0, 코드 기본값도 0.

검증: 2인 PIE에서 두 플레이어가 갑판에서 시작함을 시각 확인. Mid_1 보스의 즉시 소환, 이후 플레이어 함선을 Mid_3/Final에 접근시켰을 때 각각 Rogue/Samurai 보스 생성 로그 확인. 검증 중 이동은 PIE에만 적용하고 저장하지 않음. 검증 종료 후 에디터 모드로 복귀.

증거: `Saved/WJLast_initial_edit_report.json`, `Saved/WJLast_play_initial.json`, `Saved/WJLast_play_mid3.json`, `Saved/WJLast_final_check.json`, `Saved/Logs/ArtisticSW2026.log`.

후속 수정: 사용자 요청으로 자동 배치한 WJLast_PlayerStart_Host/Guest를 제거. 플레이어 시작점은 사용자가 직접 설정. 제가 옮긴 엔트리 위치는 사용자 추가 변경이 없는 경우에만 원래 위치로 복원. 함선의 사용자 배치는 유지.
