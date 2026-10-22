# PR #117 로컬 통합 수정 및 검증

## 기준과 작업 위치

- 대상 PR: https://github.com/Jinsoo3812/ArtisticSW2026/pull/117
- 원격 기본 브랜치는 `main`이 아니라 `master`이며, 확인한 최신 SHA는 `1746aaa1505deb34a838ec7a8620f9327530182e`다.
- PR 소스 `KKH/Level_Detail`: `f1a6386ba3e2f45c44e65a82d0d57080015b2ce0`.
- 위 두 원격 SHA는 수정 후 `git ls-remote`로 다시 대조했다.
- `Opti_Lvl/JYU`의 master 병합: `72fab6a943735101e5f1eeddb17a946a3b285e03`.
- 로컬 PR 통합 스냅샷: `227140054d79329b2fe014718fcc550f8d5b865a`.
- 수정은 `codex/fix-pr117-integration-20261022`에서 수행했다. 원래 브랜치와 병합 전 백업은 보존했다.
- 원격 push, PR 생성, PR merge는 수행하지 않았다. 기존 사용자 맵 `T1Ship_Lv1 (1)`부터 `T4Ship_Lv4 (1)`까지 수정하거나 커밋하지 않았다.

## 런타임 수정과 소유권

1. **개발용 캠페인 명령의 권한은 GameMode가 판단한다.** `CanChangeDevelopmentCampaignState`를 공유하고, 서버 권한·인증된 PlayerState·현재 게임플레이 변경 가능 상태를 확인한다. Dedicated Server의 일반 게스트는 서버 전체 대화 조건 우회나 보스/귀환 상태를 바꿀 수 없다. 등록된 호스트, Standalone, Listen Server 로컬 컨트롤러만 개발 명령을 사용할 수 있고 Shipping/Test에서는 비활성화한다. 게스트가 서버 콘솔의 일회성 요청을 먼저 소비하는 경로도 차단했다.
2. **대화 델리게이트 두 개를 동일한 서버/컴포넌트 검사 안에서 바인딩한다.** `OnBossQuestAccepted`만 조건문 밖에 있었던 중괄호 누락을 바로잡았다.
3. **항적의 CPU/GPU 계산은 같은 경계 규칙을 유지한다.** `[0, DeltaT)` 구간 소유권과 진폭 보간을 유지하며 부동소수점 반올림 수준의 root만 경계로 정규화한다. 과거의 넓은 시간 오차 허용이나 두 구간 중복 합산을 복원하지 않았다. 선형 퇴화식, 공유 끝점, 파면 전방, 진폭 보간, 수명 만료를 검증한다.
4. **캠페인 아이템도 기존 제작/인벤토리 트랜잭션을 사용한다.** 제작 검증의 결과·재료·필요 아이템에 `Item.Id`와 `Item.Quest` 계약을 일관되게 적용했다. 에디터 선택 메타데이터도 같은 범위로 맞췄다. 임의의 `Item` 분류나 `State` 태그 전체를 허용하지 않는다. 실제 `QuestItem` 행을 검증하고, CipherBook/JapaneseCipher 소비와 DecipheredCipher 지급을 테스트한다.
5. **보스 사망 연출의 끊긴 에셋 연결을 복구했다.** `docs/Boss_Death_Animation.md`와 기존 `Scripts/author_boss_death.py`의 의도에 따라 비어 있던 `BPGA_BossDeath.DeathMontage`를 Samurai 사망 몽타주로 연결했다. 마지막 포즈 유지 및 사망 완료 설정도 기존 스크립트로 재현했다. 몽타주 파일 자체는 최종 내용 차이가 없고 사망 GA만 변경되었다.

## 기존 설계를 보존한 테스트 정비

- 풀의 초기 갑판 그래프 캐시와 wake 시 재사용을 검증한다. 초기 캐시를 실패로 간주하던 기대값을 교정했고, Idle 진입이 타깃/선원을 정리하는 정상 수명주기를 우회하지 않도록 회피 테스트의 전투 타깃 설정 순서를 옮겼다. 회피 런타임 알고리즘은 변경하지 않았다.
- 선원 피해 배율 테스트는 실제 함선 소유권을 가진 선원을 등록하고 등록 개수를 먼저 확인한다. 다른 소유자의 선원 등록을 거부하는 런타임 규칙은 보존했다.
- 이동한 DataTable, 대화 UI/DA, CraftingPanel의 현재 경로를 사용한다. 남녀 Player BP 모두 대화 UI와 사망 어빌리티 연결을 검사한다. 옛 `bIsBossChest` 필드는 직렬화 호환성을 위한 deprecated/non-editable 필드라는 현재 계약을 검사한다.
- 함선 밸런스 값은 디자이너의 데이터 소유권을 따른다. 과거 지수 증가 상수를 강요하지 않고 현재 테이블/아키타입 연결, 행 해석, 양수 스탯, 티어 진행, 스킬 구성을 검증한다. 근거: `docs/Lvl_CY_EnemyShip_Editor_Setup_Audit.md`.
- 보스 테스트는 실제 T4 Samurai의 공격 세트, 공유 루트 BT, 전용 전투 서브트리 및 부여된 어빌리티를 대조한다. 서로 다른 보스의 전용 스킬 구성을 일괄 강요하지 않는다. 테스트 전용 함선도 캠페인 상태와 갑판 지형/앵커, 준비 상태를 정상 구성한다. 대기 중/생성 실패 상태에서 가상의 경비로 상자를 잠그지 않는 규칙을 보존했다.
- 입력 테스트는 프로젝트 컨트롤러, 정상 PlayerState/ASC 속성, 월드 등록을 구성해 실제 생명주기 권한 검사를 통과한다. 런타임 권한 검사를 완화하지 않았다.
- 화살 이동 테스트는 발사 프레임을 건너뛰는 현재 규칙을 존중한다. 피격 테스트는 `HandleFlightImpact`와 실제 히트 컴포넌트를 사용한다. 원거리 공격의 몽타주 없는 설정은 정상 발사를 가장하지 않고 안전한 취소를 검증한다.
- 상태이상 전달 테스트는 무태그 즉시 피해를 상태이상으로 가장하지 않는다. 실제 Poison/Burn 수신과 지연 틱을 직접 피해와 구분한다. 팀 피해 필터 검증은 별도 테스트에서 유지한다.
- 잘못된 QuestItem 검증 오류를 예상하던 등록을 제거했다. 테스트 월드에서 비동기 GameplayCue 로딩이 끝나기 전에 월드를 파괴하던 정리 경로도 보완했다. 오류 로그를 무시하는 필터를 추가하지 않았다.

## 검증 결과

- 최종 직접 UBT Editor 빌드: **성공**, exit code 0.
- 최종 전체 자동화: **159개 실행, 159개 통과, 0개 실패, 0개 미실행**, exit code 0. 124개는 경고 없이 통과, 35개는 경고와 함께 통과했다. 오류 이벤트는 0개다.
- 원래 PR 통합 회귀 96개와 제작/입력/상태이상/보스 등 변경 영향 범위를 포함한다.
- HLSL 매크로 smoke 컴파일: Windows SDK `fxc`, `ps_5_0`, 성공.
- `git diff --check`: 통과.

재현 근거는 프로젝트의 `Saved/Validation/PR117Fix_20261022/`에 있다. `VerifiedBuildFinal.log`, `VerifiedAutomationFinal/index.json`, `VerifiedAutomationFinal.log`, `VerifiedSummary.json`, `ShaderCompile.log`, `RestoreBossDeath.log`를 참조한다. Saved 보고서는 생성물이며 커밋하지 않았다.

## 검증 범위의 한계

- Unreal Engine 5.7.4, Win64 Development Editor를 직접 `UnrealBuildTool.exe`로 빌드했다. 모든 빌드와 Editor 실행을 직렬로 수행했다.
- 자동화는 `/Engine/Maps/Entry`, `-NullRHI -nosound`로 실행했다. 남녀 실제 청음, GPU 파도 화면, 네트워크 지연이 있는 여러 클라이언트 PIE 검증을 완료한 것으로 해석하면 안 된다.
- HLSL 매크로 컴파일 smoke 검사는 통과했지만 실제 GPU 장면 검증을 대신하지 않는다.
- 이 PC에는 Launcher 엔진이 설치되어 있다. PR이 가정하는 소스 엔진 및 Dedicated Server 배포본 생성 환경이 없어 해당 패키징/배포 결과는 검증하지 않았다.
- 남은 경고에는 최소 네이티브 테스트 액터의 미설정 연출/시체 저장소, 기존 Boss 상태 태그 정리, GASP의 누락된 실험용 참조 등이 포함된다. 자동화 통과를 경고 없는 콘텐츠 검증 또는 패키징 성공으로 해석하지 않는다.


## 몽타주 재확인 및 생성물 정리

수정 전 조사 결과(`BossAuthored.json`)에서 `BPGA_BossDeath.DeathMontage`는 `None`이었다. 다른 몽타주를 교체하거나 스켈레톤을 바꾸지 않았다. 추가 읽기 전용 조사에서 이 사망 GA를 사용하는 Enemy 보스 BP 4개(공통 베이스, T1, T3, T4 Samurai)는 모두 몽타주와 정확히 같은 `SK_Mannequin` 스켈레톤을 사용했다. 몽타주 슬롯은 `DefaultSlot`이다. `MontageSafetyAudit.json`에 클래스·메시·스켈레톤의 실제 경로를 남겼다. 공용 GA를 상속하는 T1/T3에도 사망 애니메이션이 적용되므로, 각 보스의 실제 화면 자세와 미술 의도는 PIE 검증 범위로 남는다.

이번 작업에서 만든 중간 빌드/테스트 로그, 일회성 조사 Python 스크립트, 중간 보고서와 확인된 크래시 자료 75개 대상을 정리해 약 146.34 MiB를 확보했다. 최종 빌드/자동화 결과, 변경 전후 몽타주 근거, 셰이더 검사 결과는 보존했다. 정리 목록과 결과는 `CleanupPlan.json`, `CleanupSummary.json`에 남겼다. 기존 재현용 `Scripts/author_boss_death.py`, 프로젝트 소스·콘텐츠, 사용자 맵, 기존 작업 자료 및 빌드 캐시는 보존했다.
