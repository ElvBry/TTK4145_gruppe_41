# rtsystem

## Project Structure

```
rtsystem
├── CMakeLists.txt
├── include
│   └── rtsystem
│       ├── common1.h
│       ├── common2.h
│       ├── ...
│       └── tasks
│           |── task1.h
│           |── task2.h
│           └── ...
└── src
    ├── main
    │   ├── CMakeLists.txt
    │   └── main.c
    └── tasks
        ├── CMakeLists.txt
        |── task1.c
        |── task2.c
        └── ...
```

- `include/rtsystem/` — shared headers
- `include/rtsystem/tasks/` — task-specific headers
- `src/tasks/` — task implementations (built as static library)
- `src/main/` — main executable
