# Git LFS 저장 구조와 디스크 고갈 대응

## 현재 로컬 배치

이 프로젝트의 `lfs.storage`는 다음 절대 경로로 설정했다.

`C:\Users\I\Documents\GitLfsStores\ArtisticSW2026`

`objects/`는 Git LFS 에셋 원본 저장소이며 `tmp/`는 변환 중간 파일이다. **이 폴더 전체는 삭제 가능한 캐시가 아니다.** 아직 원격에 올리지 않은 에셋도 포함할 수 있으므로 프로젝트와 함께 보존해야 한다. 경로는 로컬 Git 설정에만 저장되며 다른 개발자에게 적용되지 않는다. 여러 저장소가 하나의 전역 `lfs.storage`를 공유하도록 설정하지 않는다.

작업 파일과 `.git`의 위치, 브랜치, 인덱스는 유지했다. 전체 `.git` 이동 방식은 시험 저장소에서 일부 `git worktree list` 인식이 달라져 채택하지 않았다. 배포 앱 실행 파일이나 Git LFS 필터도 교체하지 않았다.

## 읽기 전용 비교의 쓰기 제거

저장 위치 분리만으로는 취소된 비교가 임시 파일을 남기는 동작이 없어지지 않았다. 따라서 이 저장소의 `filter.lfs.process`에는 검증된 `LfsDiffFilter` 어댑터를 적용했다. 시스템의 Git/Git LFS 실행 파일과 전역 필터 설정은 변경하지 않았다.

- `git diff`, `git status`, `git hash-object --stdin-paths`의 읽기 전용 경로에서는 입력을 메모리의 작은 청크로 SHA-256 계산하고 동일한 LFS 포인터를 반환한다. 전체 에셋이나 대형 임시 복사본을 저장하지 않는다. 기존 포인터와 빈 파일은 원본 의미를 유지한다.
- `add`, `commit`, `restore`, 체크아웃, `hash-object -w`와 임의 표준입력 해시는 설치된 원본 `git-lfs filter-process`에 그대로 전달한다. 실제 에셋 저장·복원과 LFS 객체 생성은 원래 구현을 사용한다.
- 호출 문맥을 확인할 수 없거나 사용자 지정 LFS 확장/변환 드라이버가 있으면 데이터 의미 보존을 우선하여 원본으로 전달한다. 이 경우 무쓰기 최적화가 적용된다고 보장하지 않는다.
- 테스트 전용 진입점은 실제 Git 필터 문맥에서 사용할 수 없으며 설치 명령에도 포함되지 않는다.

설치 위치는 `C:\Users\I\Documents\GitTools\LfsDiffFilter\<source-hash>\`이며 버전별 바이너리·소스·시험 보고서·설치 기록을 보존한다. 이 실행 파일은 Saved나 다운로드 캐시 정리 대상으로 취급하지 않는다. 검증한 바이너리 SHA-256과 설치본 SHA-256을 대조한 뒤에만 설정을 변경한다. 소스는 `Scripts/Diagnostics/LfsDiffFilter.cs`, 설치 도구는 `Scripts/Diagnostics/Install-LfsDiffFilter.ps1`이다.

Git 2.54.0.windows.1 / Git LFS 3.7.1에서 통합 시험 15개를 통과했다. 대형/작은/빈 파일, 1024바이트 경계, 기존 포인터와 CRLF 포인터의 비교 결과를 원본과 대조했다. 동시 비교 3개, 경로 기반 해시의 무쓰기, 쓰기 해시의 원본 전달, 사용자 지정 드라이버의 원본 전달, 실제 추가·커밋·smudge 복원·객체 해시, 전송 중 강제 종료 시 임시 파일 부재를 검사했다. 시험의 스테이징·커밋은 폐기 가능한 시험 저장소에만 수행했다. 실제 394MB 맵 비교도 `hash-only` 모드에서 약 0.66초에 끝났고 임시 파일 목록/크기와 인덱스 해시가 변하지 않았다.

원본과 대조하는 시험은 `Scripts/Diagnostics/Tests/test_lfs_diff_filter.py`, 결과는 `Saved/DiskDiagnosis/lfs-diff-filter-test-report.json`에 있다. Git/Git LFS나 어댑터를 갱신할 때는 다시 검증해야 한다. `git lfs install --local` 등의 재설정이 로컬 어댑터 설정을 덮어쓸 수 있으므로 실제 `filter.lfs.process`를 확인한다.

실제 저장소에서도 진단용 소스 파일의 변경/제거 5회와 동시에 대형 맵 비교(625ms), 현재 맵 및 기존 미추적 에셋의 경로 기반 해시(406ms), 앱과 같은 전체 비교(703ms)를 수행했다. 모두 `hash-only`로 성공했고 30초간 임시 파일의 최대 개수/크기는 0이었다. 진단 파일은 제거했으며 인덱스 SHA-256은 전후 동일했다. 보고서는 `Saved/DiskDiagnosis/readonly-runtime-validation.json`이다. 전체 Codex 앱 재시작과 장기간 편집까지 시험한 것으로 간주하지 않는다.

## 원인과 해결 범위

2026-10-27에 `.git/lfs/tmp`의 부분 복사본 701개가 154,563,865,618바이트를 차지했다. Git 인덱스는 1,726,050바이트였다. Codex가 실행한 Git 비교와 Git LFS 변환이 약 1초 간격으로 취소·재시작됐으며 임시 파일의 앞부분은 수정된 대형 맵과 일치했다.

Git LFS 3.7.1의 `lfs/gitfilter_clean.go`는 입력을 임시 파일에 복사한다. `commands/command_clean.go`의 정리는 변환 결과가 반환된 다음 등록된다. 진행 중인 프로세스가 강제 종료되면 이 정리 경로에 도달하지 못할 수 있다. 이는 단순한 인덱스 파일 크기 문제가 아니다.

로컬에서는 Git 비교가 만든 대형 임시 쓰기를 작업 폴더의 재귀 감시 대상에서 제거한다. 처음 적용한 임시 폴더 junction을 공식 `lfs.storage` 설정으로 대체했고, LFS 객체까지 저장소 밖의 영속 경로로 같은 볼륨 내에서 이동했다. 기존 junction은 대상 파일을 삭제하지 않고 연결 항목만 제거했다. 이 저장소의 `core.fsmonitor=false`는 유지했다. 이 설정 하나만으로는 이전 누적이 멈추지 않았다는 실험 결과가 있다.

**현재 문제의 읽기 전용 경로는 임시 파일을 정리하는 대신 처음부터 대형 복사본을 쓰지 않도록 바꿨다.** 이 경로가 취소돼도 남길 대형 중간 파일이 없으므로 파일 생성→재감지→재비교 및 부분 복사본 누적의 원인 경로를 끊는다. Codex 앱 자체의 비교 취소/재시도 로직을 수정한 것은 아니므로 불필요한 재시도가 CPU를 사용하는 문제까지 해결했다고 주장하지 않는다. 쓰기 명령의 강제 종료나 원본 전달 조건에 해당하는 다른 경로까지 임시 파일이 절대로 남지 않는다고 보장하지 않는다.

## 이전과 검증 도구

`Scripts/Diagnostics/Move-LfsStorage.ps1`는 기본적으로 계획만 출력한다. `-Apply`를 명시하면 같은 볼륨 내의 저장소 이동과 해당 저장소의 `lfs.storage` 설정 변경을 수행한다. 대상은 작업 트리 밖의 새 경로여야 하며, 기존 사용자 지정 저장소를 임의로 덮어쓰지 않는다. Git 쓰기 작업이나 잠금이 있으면 중단하고, 진행 중인 비교가 끝나기를 기다린다.

이전 전후 HEAD·인덱스 SHA-256·refs·상태 목록·worktree 목록을 비교한다. 이전 기록과 원래 Git 설정은 `C:\Users\I\Documents\GitLfsStores\MigrationReports\ArtisticSW2026`에 보존한다. 시험 저장소에서 LFS 비교·추가·커밋, 객체 해시, 임시 junction 대상 보존을 검증했다. 실제 프로젝트에서는 스테이징·커밋·푸시하지 않는다.

예시 계획 확인:

```powershell
.\Scripts\Diagnostics\Move-LfsStorage.ps1 `
  -RepositoryRoot (Get-Location).Path `
  -StorageDirectory 'C:\Users\I\Documents\GitLfsStores\ArtisticSW2026'
```

현재 설정 확인에는 `git config --local --get lfs.storage`, `git config --local --get filter.lfs.process`, `git lfs env`를 사용한다. `.git/lfs/objects`를 하드코딩하지 않는다. 이전 오디오 작업의 원본 맵 객체는 현재 저장소의 `objects/03/69/0369955d16afdd7c80d815c0ad77ad0d3aaaf5a3ab9cc17a5b51c39b9f1148ba`이며 이 파일의 SHA-256은 이름과 같다.

## 재발 시와 복구

어댑터만 해제해야 한다면 `git config --local --unset filter.lfs.process`로 이 저장소의 process 설정만 제거한다. 전역의 원본 Git LFS 필터가 다시 적용되며 `lfs.storage`는 유지한다. `filter.lfs.required`를 끄거나 객체 저장소를 삭제하지 않는다. 해제 후에는 읽기 전용 비교의 대형 임시 쓰기가 다시 발생할 수 있다. 정상 설치 복구에는 시험 보고서와 바이너리 해시를 검증하는 설치 도구를 사용한다.

여유 공간과 `git lfs env`가 가리키는 실제 임시 폴더를 함께 관찰한다. 새 파일이 생겼다가 비교 완료 후 사라지는 정상 쓰기와, 비교가 끝난 후에도 새 부분 복사본이 계속 쌓이는 현상을 구분한다. 임시 파일 삭제를 재발 해결로 보고하지 않는다. 미확인 파일을 지우거나 인덱스를 재생성하지 않는다. 진행 중인 언리얼 빌드도 강제 종료하지 않는다.

원래 저장 위치로 되돌릴 때는 Git/LFS 작업이 없는 상태에서만 수행한다. 대상 `.git/lfs`가 비어 있고 다른 데이터를 포함하지 않는지 확인한 후, 외부 LFS 저장소 디렉터리를 `git rev-parse --git-common-dir` 아래 `lfs`로 같은 볼륨 내에서 이동하고 이 저장소의 `lfs.storage` 설정만 해제한다. 객체 저장소를 삭제하거나 빈 저장소로 대체해서는 안 된다. 기존 감시 피드백 문제가 재현될 수 있으므로 원인 검증 없이 되돌리지 않는다.

상세 측정값과 당시 로그는 `Saved/DiskDiagnosis/`에 있다. 이 문서가 영속 안내이며 Saved의 기록은 보조 증거다. 앱 재시작과 장시간 대형 에셋 편집 검증의 수행 여부는 측정 보고서에서 확인해야 한다.

참고: [Git LFS 3.7.1 변환 구현](https://github.com/git-lfs/git-lfs/blob/v3.7.1/lfs/gitfilter_clean.go), [변환 결과 정리 구현](https://github.com/git-lfs/git-lfs/blob/v3.7.1/commands/command_clean.go).
