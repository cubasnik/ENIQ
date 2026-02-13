# ENIQ-like PM Parser 
 
���⮩ C++ ����� EricssonSoft Performance Management XML-䠩��� � ��࠭����� � SQLite. 
 
## ��� ᮡ��� 
cl /nologo /EHsc /O2 /std:c++17 src\*.cpp external\pugixml\pugixml.cpp external\sqlite3.c /I external\pugixml /I external /Fe:eniq.exe 

## Index management flags

The `query_db` tool supports optional SQLite index management flags to help performance and maintenance:

- `--create-indexes` : create recommended indexes on `pm_counters` and the QPI save table (best-effort)
- `--drop-indexes`   : drop those indexes (best-effort)
- `--rebuild-indexes`: drop then recreate the indexes (best-effort)

These flags are useful when running `query_db` in environments where index maintenance is required.

## Примеры CLI

Ниже несколько примеров вызовов `query_db` для управления индексами (PowerShell):

```powershell
# Создать рекомендуемые индексы (включая индекс для таблицы сохранения QPI)
.
# (предполагается, что вы находитесь в корне репозитория или указываете путь к exe)
.\build\Release\query_db.exe --create-indexes --db=eniq_data.db --save-to-db --save-table=qpi_results

# Пересоздать (удалить и создать заново)
.\build\Release\query_db.exe --rebuild-indexes --db=eniq_data.db --save-to-db --save-table=qpi_results

# Удалить индексы
.\build\Release\query_db.exe --drop-indexes --db=eniq_data.db
```

Вместо `--db=` можно указывать путь к любому sqlite-файлу. Флаги работают как "best-effort": ошибки логируются, но команда не прерывается критически.
