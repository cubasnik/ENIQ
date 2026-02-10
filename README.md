# ENIQ-like PM Parser 
 
Простой C++ парсер Ericsson Performance Management XML-файлов с сохранением в SQLite. 
 
## Как собрать 
cl /nologo /EHsc /O2 /std:c++17 src\*.cpp external\pugixml\pugixml.cpp external\sqlite3.c /I external\pugixml /I external /Fe:eniq.exe 
