
  ### How to compile Exercise2/

  Set up CMake and create a build directory:
  ```bash
  cmake -B build
  ```

  Compile using the generated Makefile:
  ```bash
  make -C build
  ```

  ### How to run executables in Exercise2/

  Executable files are inside the build directory under their own name.

  **Receivers:**
  ```bash
  ./build/udp_receiver/udp_receiver   # starts UDP receiver
  ./build/tcp_receiver/tcp_receiver   # starts TCP receiver
  ```

  **Sender:**
  ```bash
  ./build/udp_tcp_sender/udp_tcp_sender      # UDP mode (default)
  ./build/udp_tcp_sender/udp_tcp_sender -T   # TCP mode
  ```

  **Examples with options:**
  ```bash
  # Send "Hello, World!" via UDP to port 8080
  ./build/udp_tcp_sender/udp_tcp_sender -m "Hello, World!" -p 8080

  # Send "Hello, World!" via TCP to port 9000
  ./build/udp_tcp_sender/udp_tcp_sender -m "Hello, World!" -p 9000 -T
  ```

  > **Note:** Set up receivers to listen on the same port you're sending to with `-p` as well.