# 플레이어 생명주기 병합 통합 (2026-10-06)

`Opti_Lvl/JYU`에 master를 병합한 뒤 남은 사망 개발 명령·테스트를 현재 master API에 맞춘다. 캐릭터 메시, Physics Asset, 맵과 애니메이션 에셋은 이 통합 수정에서 변경하지 않는다.

## 책임과 유지하는 정책

- **MultiGameMode:** 서버의 사망 등록, 진행 상황 캡처 이후 UnPossess, 부활 세대와 타이머, 배 침몰·게임오버 상태를 관리한다. 개인 부활 대기와 시체 수명은 master의 10초를 유지한다. 부활 지점이 없으면 저장된 진행 상황으로 재시도하며, 시체가 만료돼도 스냅샷은 Controller에 남는다. 두 플레이어가 모두 죽었다는 이유만으로 레벨을 재시작하지 않는다.
- **BasePlayerController:** 동료 시점을 관전한다. 동료 시점이 없으면 마지막 생존 POV를 독립 CameraActor에 고정한다. 시체 이동·삭제에 카메라가 종속되지 않는다. 새 Pawn과 ASC가 준비되고 생존 상태가 도착하면 입력과 기본 카메라 관리를 복원한다.
- **BasePlayer:** 사망을 완료하면 서버의 등록·캡처 이후 래그돌을 표시하고, 해당 Pawn의 체력·상태·전투 표시 및 상호작용 ASC 구독을 정리한다. PlayerState의 ASC 자체와 새 Pawn의 구독은 보존한다. 종료된 Pawn은 늦은 PlayerState 복제로 ASC Avatar나 스킬 인벤토리 소스를 다시 등록하지 않는다.
- **SWDevTestInputComponent:** 모든 개발 테스트의 세션 허용·호스트 권한·UI 차단·방 복원 세대·중복 요청·요청 간격 검사를 소유한다. 사망 요청은 요청 당시 LifeCharacter와 WaitingGeneration도 전달한다. 배·대포를 조종할 때도 Controller의 LifeCharacter를 기준으로 검사한다. 교체된 Pawn, 초기화 중인 체력 또는 다른 ASC Avatar를 대상으로 실행하지 않는다.

`PlayerDeathCameraComponent`는 옛 자기 시체 추적 정책을 구현한 미사용 클래스로 제거했다. `sw.DevTest.Suicide`는 기존 KillSelf 동작에 대한 얇은 콘솔 별칭이며 별도 RPC·전역 Session CVar·부활 로직을 갖지 않는다.

발 접지는 기존 배 위 잠금 해제·애니메이션 설정을 유지한다. `Interactable`, `PlayerShipDamage`, `EnemyShipDamage` 충돌 프로필은 `FootPlacement` 트레이스를 무시하고, 실제 `ShipDeck`은 차단한다. master의 화살·대포·피해 판정 채널 응답을 보존하면서 병합으로 빠진 이 응답만 복원한다.

## 개발 테스트 사용

호스트가 로딩·복원 완료된 방의 게임 콘솔에서 테스트 세션을 허용한다.

```text
SW.DevTest.Session 1
```

실행할 플레이어의 게임 콘솔에서 로컬 입력을 켠다.

```text
SW.DevTest.Input 1
sw.DevTest.Suicide
```

자살 별칭과 Ctrl+Alt+F6는 같은 KillSelf 요청을 사용한다. Ctrl+Alt+F7은 두 명 사망, F8은 배 침몰, F9는 최종 출항이며 세 가지는 호스트만 실행할 수 있다. UI가 열렸거나 복원·전환 중이면 기존 서버 정책에 따라 거절한다. 단일 프로세스 PIE에서도 허용 상태는 방별이며 전역 CVar를 공유하지 않는다. 개발 입력의 Shipping/Test 빌드 차단은 유지한다.

종료할 때 각 창에서 `SW.DevTest.Input 0`, 호스트에서 `SW.DevTest.Session 0`을 실행한다. 콘솔 별칭은 로컬 플레이어 한 명인 월드만 대상으로 한다.

## 회귀 검증

```text
Automation RunTests ArtisticSW.Player.Death+ArtisticSW.GameMode.PlayerCorpseLifecycle+ArtisticSW.Animation.JumpAir+ArtisticSW.Animation.Stop.CapturedGait+ArtisticSW.Animation.FootPlacement+ArtisticSW.Enemy.DeathRagdoll+ArtisticSW.Development.TestInput
```

새 생명주기 테스트는 실제 GameMode·Controller·PlayerState·ASC와 캐릭터를 생성한다. 사망 진행 상황 캡처, 공유 ASC 구독 해제, 실제 부활 빙의 및 상태 복원, 늦은 시체 PlayerState·중복 완료·삭제, 오래된 요청 대상·부활 세대, 독립 관전 카메라와 입력 잠금 복원을 확인한다. 별도 GameMode 테스트는 중복 사망 등록, 오래된 타이머, 부활 지점 부재, 접속 종료 및 침몰 중 재시도 차단을 확인한다.

자동화의 임시 월드는 실제 네트워크 RPC 전송이나 전체 방 로딩을 재현하지 않는다. 남녀 Physics Asset의 외형과 움직이는 갑판 위 물리 결과, 호스트·게스트의 지연/패킷 손실, 조종 중 사망·관전·부활은 실제 2인 PIE에서 추가 확인한다. 물리 관절 튜닝은 기존 [Physics Asset 기록](Player_Death_Development_Test.md#physics-asset-조절과-현재-결정)을 참고한다.

## 검증 결과

- 엔진의 직접 `UnrealBuildTool.exe`로 UE 5.7 `ArtisticSW2026Editor Win64 Development` 빌드 성공.
- 위 통합 테스트 21개 모두 성공: 경고 없는 성공 14개, 경고 포함 성공 7개, 실패·미실행 0개. 보고서: `Saved/Automation/MergeIntegration_20261006_Final/index.json`.
- 생명주기 3개를 추가 보강 후 재실행하여 모두 성공. 재료 7개와 원래 Material 슬롯 복원, 이전 시체의 지연 이벤트·삭제 이후 새 인벤토리와 상호작용 구독 보존, 세션 미허용 개발 사망 요청의 서버 차단까지 확인했다. 보고서: `Saved/Automation/MergeIntegration_20261006_LifeFinal/index.json`.
- `git diff --check` 통과. Source/Config/Script/Shader에서 미해결 충돌 표시와 제거된 옛 사망 API 참조가 없다. 이번 수정에서 Content 에셋을 변경하지 않았다.

테스트에서 기존 `VortexPipeline` 개발 어빌리티 지급 경고 등이 남는다. 이번 검증은 해당 경고가 모두 해결되었다거나 네트워크·물리 외형까지 검증되었다는 뜻이 아니다.

### 2026-10-07 커밋 전 재검증

레벨 최적화 커밋 이후 남은 통합 수정 전체를 직접 UBT Editor Development 빌드로 다시 확인했다. 빌드는 성공했으며 기존 부력 진단 코드의 C4686 경고는 유지됐다. 위 회귀 테스트 21개도 재실행하여 성공 14개, 경고 포함 성공 7개, 실패·미실행 0개를 확인했다. 보고서는 `Saved/Automation/MergeIntegration_Commit_20261007/index.json`이다. 캐릭터·관전·개발 입력·발 접지 설정 및 해당 테스트/문서는 연결된 통합 변경으로 함께 커밋한다.
