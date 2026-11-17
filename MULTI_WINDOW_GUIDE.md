# Flutter Linux 멀티 윈도우 손코딩 가이드

목표: 메인 창의 버튼으로 두 번째 OS 창을 열고, 각 창에서 입력한 문자열을 상대 창에 표시한다. 이 프로젝트의 Linux/GTK 러너를 기준으로 한다. 아래 순서대로 직접 구현해 보자. 코드 조각은 기존 파일에 넣을 위치를 설명하는 예시이며, 문서 전체를 한 파일에 붙여 넣는 방식은 아니다.

## 1. 먼저 구조 이해하기

```text
메인 창 Dart isolate             Linux C++                 보조 창 Dart isolate
  MethodChannel ── openWindow ──> EngineManager ── 생성 ──> MethodChannel
  MethodChannel ── sendMessage ─> 라우터 ── receiveMessage ─> 화면 갱신
  MethodChannel <─ receiveMessage ── 라우터 <─ sendMessage ── MethodChannel
```

각 창은 **별도의 Flutter 엔진과 Dart isolate**를 가진다. 같은 채널 이름을 쓰더라도 messenger는 엔진마다 다르므로 창끼리 직접 연결되지 않는다. 네이티브가 창 ID로 대상 채널을 찾아 중계해야 한다. `setState`나 Dart 전역 변수도 창끼리 공유되지 않는다.

약속할 프로토콜:

| 방향 | 메서드 | 인자/결과 |
| --- | --- | --- |
| Dart → native | `getWindowId` | 결과: `main` 또는 `child` |
| Dart → native | `openWindow` | 결과: 성공 시 `null` |
| Dart → native | `sendMessage` | `{ "target": "child", "text": "안녕" }`; 결과: 성공 시 `null` |
| native → Dart | `receiveMessage` | `{ "from": "main", "text": "안녕" }`; 결과: `null` |

채널 이름은 양쪽 모두 `multi_windows/window`로 통일한다. 이 실습은 `main`, `child` 각 하나만 관리한다.

## 2. 기존 `engine_manager.h` 먼저 수정하기

현재 헤더의 `fl_engine_start()`는 설치된 Flutter Linux 공개 헤더에 선언된 API가 아니다. 또한 `fl_view_new_for_engine()`은 기존 엔진에 추가 뷰를 붙이는 API여서, 새 엔진의 기본 뷰를 만들려는 이 실습과 다르다. 기본 러너처럼 `fl_view_new(project)`를 쓰면 새 엔진과 기본 뷰가 생성되고, 뷰를 realize할 때 엔진이 시작된다.

`EngineManager::create`에서 엔진/뷰 생성 부분을 아래 형태로 바꿔 보자.

```cpp
g_autoptr(FlDartProject) project = fl_dart_project_new();
FlView* view = fl_view_new(project);
FlEngine* engine = fl_view_get_engine(view);  // 빌린 참조
GtkWidget* window = gtk_application_window_new(app);
gtk_window_set_default_size(GTK_WINDOW(window), 1280, 720);
gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view));
```

- 기존의 `fl_engine_new`, `fl_engine_start`, `fl_view_new_for_engine` 호출과 실패 시 `g_object_unref(engine)` 부분을 제거한다.
- `fl_view_get_engine`으로 얻은 포인터는 빌린 참조다. 현재 소멸자의 `g_object_unref(entry.engine)`도 제거한다. `view`와 `window`는 GTK 위젯 생명주기에 맞춰 관리한다.
- `gtk_widget_show_all(window)`로 바로 보여 주기 전에 해당 엔진의 채널과 플러그인을 등록한다. 기본 러너처럼 `first-frame` 신호에서 창을 보여 주고, `gtk_widget_realize(GTK_WIDGET(view))`로 렌더링을 시작할 수 있다.
- `create()`가 `std::vector` 원소의 포인터를 반환한다. 이후 `push_back`으로 재할당되면 그 포인터가 무효가 되므로 콜백에 저장하지 말자. ID를 저장하고 필요할 때 `get(id)`로 다시 찾거나, 안정적인 주소를 보장하는 자료구조를 선택한다.
- 창의 `destroy` 신호에서 엔트리와 채널을 제거해야 한다. 닫힌 창의 포인터를 재사용하면 안 되고, 지운 뒤 같은 ID로 다시 열 수 있어야 한다. 핸들러의 `user_data`도 그때까지 유효하도록 소유권을 정한다.

메인 창은 지금 `my_application_activate()`가 직접 생성한다. 메인 창도 동일한 라우터에서 찾을 수 있도록 `main`으로 등록하자. 새 창만 `EngineManager`에 등록하고 메인을 빠뜨리면 child → main 전송이 실패한다. `MyApplication`은 GObject이므로 C++ 객체를 구조체 안에 값으로 추가하지 말고 `EngineManager*`를 생성·해제하는 방식 등을 사용한다. 종료 시 창/채널과 매니저의 생명주기를 일관되게 정리하자.

## 3. 엔진마다 네이티브 채널 등록하기

`my_application.cc`에서 기존 뷰를 만드는 직후, 그리고 새 창의 뷰를 만드는 직후 각각 다음 순서로 처리한다.

1. `fl_view_get_engine(view)`로 해당 엔진을 얻는다.
2. 그 엔진의 `fl_engine_get_binary_messenger(engine)`로 채널을 만든다.
3. 채널에 메서드 핸들러를 등록하고 창 ID와 매니저를 핸들러 문맥에 연결한다.
4. 각 뷰에 `fl_register_plugins(FL_PLUGIN_REGISTRY(view))`를 호출한다. 플러그인은 엔진별로 등록해야 한다.
5. 뷰를 realize하고 첫 프레임 이후 창을 표시한다. Dart가 시작되기 전에 핸들러를 등록하는 것이 중요하다.

채널 생성 예시:

```cpp
constexpr char kChannelName[] = "multi_windows/window";

FlBinaryMessenger* messenger = fl_engine_get_binary_messenger(engine);
g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
FlMethodChannel* channel = fl_method_channel_new(
    messenger, kChannelName, FL_METHOD_CODEC(codec));
fl_method_channel_set_method_call_handler(
    channel, on_method_call, context, nullptr);
```

`channel`의 소유 참조를 엔트리에 보관하고 창 정리 시 핸들러를 해제한 뒤 `g_object_unref`한다. `context`에는 창 ID와 매니저 포인터를 넣고, 콜백이 살아 있는 동안 유효하게 유지한다. `destroy_notify`를 사용해 문맥을 해제해도 된다. 메인 창과 보조 창에 각각 **별개의** 채널 인스턴스를 만든다.

## 4. Dart → native 요청 처리하기

핸들러 형태와 응답의 뼈대:

```cpp
static void on_method_call(FlMethodChannel* channel,
                           FlMethodCall* call,
                           gpointer user_data) {
  // context에서 호출한 창 ID와 EngineManager를 얻는다.
  const gchar* method = fl_method_call_get_name(call);
  if (g_strcmp0(method, "getWindowId") == 0) {
    // 아래 id는 context에 저장한 호출 창 ID로 바꾼다.
    const char* id = /* 호출 창 ID */;
    g_autoptr(FlValue) result = fl_value_new_string(id);
    fl_method_call_respond_success(call, result, nullptr);
  } else if (g_strcmp0(method, "openWindow") == 0) {
    // child가 없으면 생성하고 채널/플러그인을 등록한다.
    // 이미 있으면 기존 창에 포커스를 주거나 오류를 반환한다.
    fl_method_call_respond_success(call, nullptr, nullptr);
  } else if (g_strcmp0(method, "sendMessage") == 0) {
    // 아래 5절의 라우팅과 입력 검증을 구현한다.
  } else {
    fl_method_call_respond_not_implemented(call, nullptr);
  }
}
```

각 분기에서 **정확히 한 번** 응답한다. 생성 실패나 잘못된 인자는 `fl_method_call_respond_error(call, "error_code", "설명", nullptr, nullptr)`로 돌려준다. 위 `openWindow` 예시의 성공 응답은 실제 생성 성공 후에만 호출한다. 자식 창 중복 생성 정책은 기존 창 포커스로 정하면 실습하기 쉽다.

## 5. native에서 대상 창으로 중계하기

`sendMessage` 분기에서 `fl_method_call_get_args(call)`을 읽고 다음을 구현한다.

1. 인자가 `FL_VALUE_TYPE_MAP`인지 확인한다.
2. `fl_value_lookup_string(args, "target")`, `fl_value_lookup_string(args, "text")`로 값을 읽고 둘 다 문자열인지 검사한다. 문자열은 `fl_value_get_string`으로 얻는다.
3. 매니저에서 `target`에 해당하는 **살아 있고 준비된** 창과 그 채널을 찾는다. 없으면 `window_unavailable` 오류로 응답한다.
4. 전달할 map에 호출자의 ID와 텍스트를 넣고 대상 채널에 `receiveMessage`를 호출한다.

```cpp
g_autoptr(FlValue) payload = fl_value_new_map();
fl_value_set_string_take(payload, "from", fl_value_new_string(source_id));
fl_value_set_string_take(payload, "text", fl_value_new_string(text));
fl_method_channel_invoke_method(target_channel, "receiveMessage", payload,
                                nullptr, nullptr, nullptr);
fl_method_call_respond_success(call, nullptr, nullptr);
```

여기서 성공은 **네이티브가 전송을 요청했다**는 뜻이다. Dart가 실제 처리했는지 확인하려면 콜백과 `fl_method_channel_invoke_method_finish`로 응답을 처리해야 한다.

초기화 경합을 피하려면 Dart가 `setMethodCallHandler`를 설치한 뒤 native에 `ready`를 호출하게 하고, native는 호출한 창을 준비 완료로 표시한다. 위 라우터의 `ready` 분기를 추가하고 성공 응답을 보내자. `openWindow`의 성공은 창 생성 요청의 성공이지, 자식 Dart 핸들러가 준비됐다는 뜻이 아니다. 준비 전 메시지는 `window_unavailable`로 처리하고 UI에서 다시 보내거나, 별도의 준비 알림을 구현한다.

## 6. Dart 화면 만들기

`lib/main.dart`에서 Flutter 표준 UI를 쓰려면 `package:flutter/material.dart`와 `package:flutter/services.dart`를 import한다. 각 엔진은 기본적으로 같은 `main()`을 실행하므로, 시작 후 `getWindowId`로 자신의 역할을 받아 UI를 구분한다.

State 안에 다음 채널과 상태를 두자.

```dart
static const channel = MethodChannel('multi_windows/window');
String? windowId;
String received = '';
final input = TextEditingController();
```

`initState`에서 `super.initState()` 다음에 핸들러를 먼저 등록하고, 비동기 초기화 메서드를 호출한다.

```dart
channel.setMethodCallHandler((call) async {
  if (call.method == 'receiveMessage') {
    final data = Map<Object?, Object?>.from(call.arguments as Map);
    if (mounted) {
      setState(() => received = '${data['from']}: ${data['text']}');
    }
  }
});
_initialize();
```

`_initialize()`에서 순서대로 `getWindowId`를 호출해 `windowId`를 저장하고, 핸들러 준비가 끝났음을 `ready`로 알린다. 각 `await` 뒤 UI를 갱신하기 전에는 `mounted`를 확인하고 `PlatformException`을 화면에 표시한다.

```dart
final id = await channel.invokeMethod<String>('getWindowId');
await channel.invokeMethod<void>('ready');
if (!mounted) return;
setState(() => windowId = id);
```

화면에 `TextField(controller: input)`, 받은 메시지 `Text(received)`, 전송 버튼을 배치한다. 메인일 때만 창 열기 버튼을 표시한다. 각 버튼의 핵심 호출:

```dart
await channel.invokeMethod<void>('openWindow');

await channel.invokeMethod<void>('sendMessage', {
  'target': windowId == 'main' ? 'child' : 'main',
  'text': input.text,
});
```

두 호출은 각각 별도의 버튼 콜백에 넣는다. 초기화 전에는 버튼을 비활성화하고, `window_unavailable` 등 `PlatformException`은 `SnackBar` 등에 표시한다. `dispose`에서 `input.dispose()`와 `channel.setMethodCallHandler(null)`을 호출한다.

## 7. 확인 순서

1. `flutter analyze`로 Dart 오류를 확인한다.
2. `flutter run -d linux`로 실행한다. C++을 바꿨다면 앱을 완전히 종료하고 다시 빌드한다. Hot reload는 네이티브 코드를 다시 빌드하지 않는다.
3. 메인 창에 `main`이 표시되고 버튼으로 `child` 창이 열리는지 확인한다.
4. 두 창의 초기화가 끝난 후 main → child, child → main 메시지를 각각 보낸다.
5. child를 닫은 뒤 전송하면 오류가 표시되는지, 다시 열면 통신이 복구되는지 확인한다. 버튼을 연속으로 눌러도 중복 창이 생기지 않는지 확인한다.

문제 해결: `MissingPluginException`이면 채널 이름과 **해당 엔진**에 핸들러가 등록됐는지 확인한다. 메시지가 처음에만 사라지면 `ready` 순서와 대상 창의 준비 상태를 확인한다. 종료 시 오류가 나면 `destroy` 정리, 채널 참조, 콜백 문맥 및 빌린 엔진 참조를 확인한다.
