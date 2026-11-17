import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:multi_windows_poc/main.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('opens a post using its ID', (tester) async {
    final calls = <MethodCall>[];
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(windowChannel, (call) async {
          calls.add(call);
          return null;
        });
    addTearDown(() => TestDefaultBinaryMessengerBinding.instance
        .defaultBinaryMessenger.setMockMethodCallHandler(windowChannel, null));

    await tester.pumpWidget(MaterialApp(home: Builder(builder: (context) =>
        TextButton(onPressed: () => openWindow(context, 'post', 42), child: const Text('Open')))));
    await tester.tap(find.text('Open'));
    await tester.pump();
    expect(calls.single.method, 'openWindow');
    expect(calls.single.arguments, {'type': 'post', 'id': 42});
  });
}
