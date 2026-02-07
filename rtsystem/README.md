# rtsystem

## Project Structure
From tree:
```
rtsystem
.
├── CMakeLists.txt
├── include
│   └── rtsystem
│       ├── async_log_helper.h
│       ├── core
│       │   ├── fifo_queue.h
│       │   └── task_helper.h
│       ├── log_helper.h
│       └── tasks
│           ├── example_worker_task.h
│           └── log_task.h
├── README.md
└── src
    ├── core
    │   ├── CMakeLists.txt
    │   ├── fifo_queue.c
    │   └── task_helper.c
    ├── main
    │   ├── CMakeLists.txt
    │   └── main.c
    └── tasks
        ├── CMakeLists.txt
        ├── example_worker_task.c
        └── log_task.c

9 directories, 16 files
```

- `include/rtsystem/`       — shared headers
- `include/rtsystem/tasks/` — task-specific headers
- `include/rtsystem/core/`  — core-specific headers
- `src/core/`  — core implementations (built as static library)
- `src/tasks/` — task implementations (built as static library)
- `src/main/`  — main executable


## How to build, compile and run project
Set up CMake and with build directory:
```bash
cmake -B build # optionally add '-DASYNC_LOG=ON' or '-DASYNC_LOG=OFF' to configure logging
```

Compile using the generated Makefile:
```bash
make -C build
```

Run executable with:
```bash
sudo ./build/src/main/rtsystem
```