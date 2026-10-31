# 날씨 / 시간 콘솔 명령

Development 빌드에서 방 호스트 또는 Standalone 플레이어가 실행한다. 별도의 DevTest Session 활성화는 필요 없다.

```text
SW.Weather.Preset
SW.Weather.Preset Sunny
SW.Weather.Preset Cloudy
SW.Weather.Preset Rainy
SW.Weather.Preset Thunder
SW.Time.Set Morning
SW.Time.Set Noon
SW.Time.Set Evening
SW.Time.Set Night
```

- 인수 없는 `SW.Weather.Preset`은 실제 Blueprint enum의 ID와 이름을 출력한다.
- 날씨 이름은 대소문자를 구분하지 않는다. Clear → Sunny, Rain → Rainy, Storm → Thunder 별칭을 지원한다. 실제 enum에 없는 이름은 거부한다.
- 시간대: Morning 08:00, Noon 12:00, Evening 18:00, Night 22:00.
- 날씨 변경은 현재 시각을 분 단위로 유지한다. 시간 변경은 현재 날씨 구간의 CurrentParam에 기록된 날씨 종류를 유지한다. 전환 중인 시각적 혼합 상태 자체를 고정하는 기능은 아니다.
- 기존 `SW.Weather.Set <시 0-23> <분 0-59> <날씨 ID>`도 유지한다.
- 변경 뒤 기존 자동 시간 진행과 날씨 추첨은 계속된다. 시간/날씨 고정 명령이 아니다.

서버가 호스트 권한을 검사한 뒤 `SetNetworkWeatherTime`으로 새 epoch와 playback window를 생성한다. 기존 복제/클라이언트 재생/중간 접속 경로를 그대로 사용한다. 현재 상태는 미래 PlannerState나 호스트의 로컬 표시 속성이 아닌, 서버 history의 재생 시각(서버 시각 - 2초)에서 선택하므로 Dedicated Server도 동일한 경로를 쓴다.

## Implementation Log & Checklist

- [x] 이름 기반 날씨 명령과 네 가지 시간대 명령 추가. 기존 숫자 명령 유지.
- [x] 서버 호스트 검사와 기존 복제 경로 연결. 선박 이동 코드 및 진단 로그 변경 없음.
- [x] `git diff --check` 통과. UnrealHeaderTool 헤더 처리 통과.
- [x] 후속 재시도에서 기존 MSVC 14.44.35229(설치 폴더 14.44.35207), `-NoUBA`로 ArtisticSW2026Editor Win64 Development 컴파일 및 최종 링크 성공. 앞선 시도는 D8050 DLL 로드 차단으로 실패했으나 같은 컴파일러로 재시도하여 해결했다. 확인된 날씨 코드 C4456 변수명 중복도 수정했다.
- [x] 후속 점프 변경은 사용자 요청으로 전부 롤백했다. 점프 코드와 문서는 대화 시작 전 상태로 복구했으며, 날씨·시간 명령 변경은 유지한다.
- [ ] 호스트/게스트/중간 접속 실행 검증: 수행하지 않음.
