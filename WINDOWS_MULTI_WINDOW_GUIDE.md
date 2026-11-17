챙ㄷ# Windows 멀티 윈도우

Linux와 동일하게 Dart의 `multi_windows/window` 메서드 채널을 사용한다. 메인 창의 게시글을 선택하면 게시글 상세 창이, 상세 창의 작성자를 선택하면 사용자 상세 창이 열린다. 각 창은 독립적인 Flutter 엔진을 사용하며, 같은 게시글이나 사용자의 창이 이미 열려 있으면 기존 창을 앞으로 가져온다. 마지막 창을 닫으면 앱이 종료된다.

## 구성

- `windows/runner/engine_manager.h`: 보조 창 생성과 조회 인터페이스. Linux의 `EngineManager`와 마찬가지로 `create`와 `get`을 사용한다. 실제 GTK와 Win32 타입이 다르므로 함수 인자는 플랫폼에 맞게 다르다.
- `windows/runner/engine_manager.cpp`: Win32 창 및 엔진의 소유와 정리.
- `windows/runner/flutter_window.cpp`: 창별 Flutter 엔진과 메서드 채널 등록, 창 열기 요청 처리.
- `lib/main.dart`: Windows와 Linux가 공유하는 화면과 API 요청.

채널 메서드도 Linux와 동일하다.

- `getWindowInfo` → `{type: 'main' | 'post' | 'user', id: int}`
- `openWindow`에 `{type: 'post' | 'user', id: int}` 전달

닫힌 보조 창은 재생성할 때 관리 목록에서 정리된다. 반환된 `EngineEntry*`는 관리자가 해당 창을 정리하거나 종료하면 더 이상 유효하지 않으므로 오래 보관하지 않는다.

## 실행

Windows에서 Flutter의 Windows 데스크톱 개발 환경과 Visual Studio의 C++ 데스크톱 워크로드를 준비한 후 프로젝트 루트에서 실행한다.

```powershell
flutter pub get
flutter run -d windows
```

실행 후 게시글 클릭 → 작성자 보기 → 사용자 정보와 게시글 목록을 확인한다. 창을 닫고 다시 열거나, 같은 항목을 두 번 클릭하는 경우도 확인한다. API를 가져오기 위해 인터넷 연결이 필요하다. 네이티브 C++ 수정 후에는 앱을 종료하고 다시 빌드한다.

Windows의 Flutter `DartProject`에는 별도 Dart 진입점을 설정하는 API도 있지만, 이 앱에서는 Linux와 같은 코드와 채널 프로토콜을 사용하기 위해 모든 엔진이 기본 `main()`으로 시작한다.
