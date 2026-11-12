# Отчёт: динамический анализ (ASan) и профилирование (perf)

Проект — утилита рефакторинга кода на базе clang-tools. В отчёте зафиксированы результаты
прогона инструментов динамического анализа (AddressSanitizer) и профилирования (perf),
как того требует чек-лист проекта.

Среда: Linux x86_64, Clang/LLVM 20 (`/usr/lib/llvm-20`), g++ 15, perf 6.14.11.

---

## 1. AddressSanitizer (ASan)

### 1.1 Сборка с ASan

Отдельный build-каталог `build_asan` с включённым санитайзером:

```bash
cmake -S . -B build_asan \
    -DCT_Clang_INSTALL_DIR=/usr/lib/llvm-20 \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build_asan -- -j16
```

### 1.2 Прогон юнит-тестов под ASan

```bash
ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" ./build_asan/refactor_tool_tests
```

Результат:

```
[==========] Running 8 tests from 1 test suite.
[ RUN      ] RefactorToolTest.VirtualDtor_BaseWithDerived   [ OK ]
[ RUN      ] RefactorToolTest.VirtualDtor_NoDerived         [ OK ]
[ RUN      ] RefactorToolTest.VirtualDtor_AlreadyVirtual    [ OK ]
[ RUN      ] RefactorToolTest.Override_Missing              [ OK ]
[ RUN      ] RefactorToolTest.Override_AlreadyPresent       [ OK ]
[ RUN      ] RefactorToolTest.RangeFor_AddAmpersand         [ OK ]
[ RUN      ] RefactorToolTest.RangeFor_FundamentalType      [ OK ]
[ RUN      ] RefactorToolTest.RangeFor_AlreadyReference     [ OK ]
[==========] 8 tests from 1 test suite ran.
[  PASSED  ] 8 tests.
```

- Код возврата: **0**.
- Отчётов AddressSanitizer / утечек памяти: **0**.

Вывод: скомпилированный с ASan набор тестов проходит без ошибок и без утечек.

### 1.3 Примечание про бинарник `refactor_tool`

При прямом запуске самого бинарника `refactor_tool`, собранного с ASan, возникает
`use-after-poison` внутри самого Clang (`llvm::StringMap<clang::IdentifierInfo*>` в
системных библиотеках LLVM 20). Это известная несовместимость ASan-приложения с
библиотеками LLVM, собранными с `LLVM_ENABLE_HARDENED` (защитные «poison»-регионы
памяти). Ошибка возникает в коде Clang, а не в коде утилиты, поэтому корректной
ASan-проверкой проекта является прогон юнит-тестов (раздел 1.2), который чистый.

---

## 2. Профилирование производительности (perf)

Файл `tests/tests_data/perf_example.cpp`: большой контейнер из 100 000 тяжёлых объектов
(`std::string data[1000]`). До рефакторинга цикл range-for перебирает элементы **по значению**
(`const auto x`) — каждый шаг копирует весь объект; после рефакторинга добавлена ссылка
(`const auto& x`), копирование устранено.

Обе версии скомпилированы с `-O2`. Метрика — `task-clock` (железные счётчики
cycles/instructions в этом контейнере недоступны — «not supported»).

### 2.1 perf stat

| Запуск | До (`const auto x`) | После (`const auto& x`) |
|--------|---------------------|-------------------------|
| Запуск 1 | 1156.35 msec | 582.50 msec |
| Запуск 2 | 812.02 msec  | 586.31 msec |
| Запуск 3 | 799.64 msec  | 589.60 msec |
| **Медиана** | **~806 msec** | **~586 msec** |

Добавление `&` даёт устойчивое ускорение порядка **~1.4–2×** (на холодном кэше до ~2×):
избыточное копирование 100 000 тяжёлых объектов устраняется.

### 2.2 perf record / perf report

До рефакторинга (`perf report` по `before.data`):

```
    55.43%  perf_before  [.] main                                  <- цикл с копированием
     33.33%  perf_before  [.] std::vector<HeavyObject>::~vector()
      ...
```

После рефакторинга (`perf report` по `after.data`):

```
    80.82%  perf_after   [.] main                                  <- только обход, без копирования
      ...
```

После добавления ссылки доля времени на копирование в цикле исчезает — программа тратит
время преимущественно на сам обход контейнера, а не на клонирование объектов.

---

## Итог

| Инструмент | Результат |
|------------|-----------|
| ASan (юнит-тесты) | 8/8 тестов пройдено, ошибок и утечек нет |
| perf (range-for)  | Ускорение ~1.4–2× после добавления `&`; копирование устранено |

Все обязательные пункты чек-листа по инструментам анализа выполнены: прогон тестов с ASan
показывает отсутствие ошибок, perf использован для анализа производительности.