# kv

Хранилище ключ - значение. Ключом владеет актор `hash(key) % число акторов`: сервер на каждом акторе хранит свои
ключи, за чужими идёт к серверу владельца

- [kv.yaml](kv.yaml) - API
- [store.cpp](store.cpp) - обработчики
- [main.cpp](main.cpp) - запуск на 127.0.0.1:8080
- [test_store.cpp](test_store.cpp) - тесты

Сборка: `-Dbtye_ENABLE_EXAMPLES=ON`, цель `btye_kv`

```
curl --http2-prior-knowledge -X PUT localhost:8080/keys/hello -H 'content-type: text/plain' -d world
curl --http2-prior-knowledge localhost:8080/keys/hello
curl --http2-prior-knowledge localhost:8080/size
curl --http2-prior-knowledge -X DELETE localhost:8080/keys/hello
```
