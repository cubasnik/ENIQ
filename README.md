ENIQ parser

Build (Linux/macOS):

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

Or compile directly (example):

```sh
g++ -std=c++17 -O2 -Wall src/main.cpp src/xml_parser.cpp src/db_writer.cpp external/pugixml/pugixml.cpp -lpqxx -lpq -o eniq_parser
```

Notes:
- Put test XML files into the `data/` directory.
- Place `pugixml.hpp` and `pugixml.cpp` into `external/pugixml/` (see external/pugixml/README.txt).
- Adjust PostgreSQL connection string in `src/main.cpp` or use environment-based configuration.
