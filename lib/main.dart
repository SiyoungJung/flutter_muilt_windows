import 'dart:convert';
import 'dart:io';

import 'package:material_ui/material_ui.dart';
import 'package:flutter/services.dart';

const windowChannel = MethodChannel('multi_windows/window');

void main() => runApp(const PostsApp());

class Api {
  static Future<dynamic> get(String path, [Map<String, String>? query]) async {
    final client = HttpClient();
    try {
      final uri = Uri.https('jsonplaceholder.typicode.com', path, query);
      final request = await client
          .getUrl(uri)
          .timeout(const Duration(seconds: 15));
      final response = await request.close().timeout(
        const Duration(seconds: 15),
      );
      if (response.statusCode != HttpStatus.ok) {
        throw HttpException('서버 오류 (${response.statusCode})', uri: uri);
      }
      return jsonDecode(await utf8.decoder.bind(response).join());
    } finally {
      client.close(force: true);
    }
  }
}

class PostsApp extends StatelessWidget {
  const PostsApp({super.key});

  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'Posts',
    theme: ThemeData(
      colorScheme: ColorScheme.fromSeed(seedColor: Colors.indigo),
      useMaterial3: true,
    ),
    home: const WindowScreen(),
  );
}

class WindowScreen extends StatefulWidget {
  const WindowScreen({super.key});

  @override
  State<WindowScreen> createState() => _WindowScreenState();
}

class _WindowScreenState extends State<WindowScreen> {
  late Future<Map<Object?, Object?>> window;

  @override
  void initState() {
    super.initState();
    window = loadWindow();
  }

  Future<Map<Object?, Object?>> loadWindow() async {
    final info = await windowChannel.invokeMapMethod<Object?, Object?>(
      'getWindowInfo',
    );
    if (info == null) throw StateError('창 정보를 가져오지 못했습니다.');
    return info;
  }

  @override
  Widget build(BuildContext context) => FutureBuilder(
    future: window,
    builder: (context, snapshot) {
      if (snapshot.hasError) {
        return Scaffold(
          body: Center(child: Text('창을 초기화하지 못했습니다: ${snapshot.error}')),
        );
      }
      if (!snapshot.hasData) {
        return const Scaffold(body: Center(child: CircularProgressIndicator()));
      }
      final info = snapshot.data!;
      final id = info['id'] as int;
      return switch (info['type']) {
        'main' => const PostsScreen(),
        'post' => PostScreen(id: id),
        'user' => UserScreen(id: id),
        _ => const Scaffold(body: Center(child: Text('알 수 없는 창입니다.'))),
      };
    },
  );
}

Future<void> openWindow(BuildContext context, String type, int id) async {
  try {
    await windowChannel.invokeMethod<void>('openWindow', {
      'type': type,
      'id': id,
    });
  } on PlatformException catch (error) {
    if (context.mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('창을 열지 못했습니다: ${error.message ?? error.code}')),
      );
    }
  }
}

class AsyncContent<T> extends StatelessWidget {
  const AsyncContent({
    super.key,
    required this.future,
    required this.onRetry,
    required this.builder,
  });
  final Future<T> future;
  final VoidCallback onRetry;
  final Widget Function(T) builder;

  @override
  Widget build(BuildContext context) => FutureBuilder<T>(
    future: future,
    builder: (context, snapshot) {
      if (snapshot.hasError) {
        return Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(
                '데이터를 불러오지 못했습니다.\n${snapshot.error}',
                textAlign: TextAlign.center,
              ),
              const SizedBox(height: 12),
              OutlinedButton(onPressed: onRetry, child: const Text('다시 시도')),
            ],
          ),
        );
      }
      if (!snapshot.hasData) {
        return const Center(child: CircularProgressIndicator());
      }
      return builder(snapshot.data as T);
    },
  );
}

typedef Json = Map<String, dynamic>;

class PostsScreen extends StatefulWidget {
  const PostsScreen({super.key});
  @override
  State<PostsScreen> createState() => _PostsScreenState();
}

class _PostsScreenState extends State<PostsScreen> {
  late Future<List<Json>> posts;
  @override
  void initState() {
    super.initState();
    load();
  }

  void load() =>
      posts = Api.get('/posts').then((data) => (data as List).cast<Json>());

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(title: const Text('게시글 목록')),
    body: AsyncContent<List<Json>>(
      future: posts,
      onRetry: () => setState(load),
      builder: (posts) => ListView.separated(
        itemCount: posts.length,
        separatorBuilder: (_, _) => const Divider(height: 1),
        itemBuilder: (context, index) {
          final post = posts[index];
          return ListTile(
            leading: CircleAvatar(child: Text('${post['id']}')),
            title: Text(post['title'] as String),
            subtitle: Text(
              post['body'] as String,
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
            ),
            trailing: const Icon(Icons.open_in_new),
            onTap: () => openWindow(context, 'post', post['id'] as int),
          );
        },
      ),
    ),
  );
}

class PostScreen extends StatefulWidget {
  const PostScreen({super.key, required this.id});
  final int id;
  @override
  State<PostScreen> createState() => _PostScreenState();
}

class _PostScreenState extends State<PostScreen> {
  late Future<Json> post;
  @override
  void initState() {
    super.initState();
    load();
  }

  void load() =>
      post = Api.get('/posts/${widget.id}').then((data) => data as Json);

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(title: Text('게시글 #${widget.id}')),
    body: AsyncContent<Json>(
      future: post,
      onRetry: () => setState(load),
      builder: (post) => SingleChildScrollView(
        padding: const EdgeInsets.all(32),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              post['title'] as String,
              style: Theme.of(context).textTheme.headlineMedium,
            ),
            const SizedBox(height: 24),
            Text(
              post['body'] as String,
              style: Theme.of(context).textTheme.bodyLarge,
            ),
            const SizedBox(height: 32),
            OutlinedButton.icon(
              onPressed: () =>
                  openWindow(context, 'user', post['userId'] as int),
              icon: const Icon(Icons.person_outline),
              label: Text('작성자 #${post['userId']} 보기'),
            ),
          ],
        ),
      ),
    ),
  );
}

class UserScreen extends StatefulWidget {
  const UserScreen({super.key, required this.id});
  final int id;
  @override
  State<UserScreen> createState() => _UserScreenState();
}

class _UserScreenState extends State<UserScreen> {
  late Future<List<dynamic>> data;
  @override
  void initState() {
    super.initState();
    load();
  }

  void load() => data = Future.wait([
    Api.get('/users/${widget.id}'),
    Api.get('/posts', {'userId': '${widget.id}'}),
  ]);

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(title: const Text('작성자 정보')),
    body: AsyncContent<List<dynamic>>(
      future: data,
      onRetry: () => setState(load),
      builder: (data) {
        final user = data[0] as Json;
        final posts = (data[1] as List).cast<Json>();
        final address = user['address'] as Json;
        final company = user['company'] as Json;
        return ListView(
          padding: const EdgeInsets.all(24),
          children: [
            Text(
              user['name'] as String,
              style: Theme.of(context).textTheme.headlineMedium,
            ),
            Text(
              '@${user['username']}',
              style: Theme.of(context).textTheme.titleMedium,
            ),
            const SizedBox(height: 16),
            Text('이메일: ${user['email']}'),
            Text('전화: ${user['phone']}'),
            Text('웹사이트: ${user['website']}'),
            Text('회사: ${company['name']}'),
            Text(
              '주소: ${address['city']}, ${address['street']}, ${address['suite']}',
            ),
            const SizedBox(height: 24),
            Text(
              '작성한 글 ${posts.length}개',
              style: Theme.of(context).textTheme.titleLarge,
            ),
            const Divider(),
            for (final post in posts)
              ListTile(
                contentPadding: EdgeInsets.zero,
                title: Text(post['title'] as String),
                subtitle: Text(post['body'] as String),
              ),
          ],
        );
      },
    ),
  );
}
