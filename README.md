# Mini_HTTP

## 项目简介

Mini_HTTP 是一个基于 C++17 和 Linux Socket API 的 HTTP 服务器学习项目，
在 WSL / Ubuntu 环境下开发。

项目从阻塞 Socket 开始，逐步实现 HTTP 解析、非阻塞读写、Reactor、
线程池、静态文件服务和 HTTP/1.1 Keep-Alive，目前已实现 Phase 1～Phase 7 的功能。

服务器采用单 IO 线程的 LT epoll 事件循环，配合 ThreadPool 处理业务。
IO 线程负责 Socket 收发和连接生命周期，worker 负责文件读取与响应生成，
通过任务队列和 eventfd 将结果交回 IO 线程。同一连接内的请求串行处理。

## 当前功能与进度

### Phase 1：最小 HTTP 服务器

- 使用 socket、bind、listen、accept 建立 TCP 服务。
- 完成请求接收、固定响应发送及连接关闭。

### Phase 2：HTTP 请求解析与响应生成

- 解析请求行、Headers 和 Content-Length 指定的定长 Body。
- 区分完整请求、分段请求和非法请求。
- 生成 HTTP 状态行、响应头及正文，支持 200、400、404、405 等响应。

### Phase 3：非阻塞 Socket 与 epoll

- 使用 LT epoll 管理多个客户端连接，收发操作采用非阻塞方式。
- 保存请求缓冲区、响应缓冲区及发送进度，支持分段接收与部分发送。
- 处理 EINTR、EAGAIN/EWOULDBLOCK 和连接异常，及时释放资源。

### Phase 4：Reactor 与 ThreadPool

- 用 Server、EventLoop、Acceptor、Connection 拆分协调、事件分发、监听和连接职责。
- 使用任务队列、互斥锁和条件变量调度 worker，通过 EventLoop::post 回传结果。
- 使用 fd/id 校验迟到结果的归属，通过 RAII 管理文件描述符和线程池生命周期。

### Phase 5：静态文件服务

- 默认以 www 为静态文件根目录，GET / 映射到 index.html。
- 支持嵌套路径、查询参数分离、百分号解码和路径边界检查。
- 支持常见文件类型、空文件和二进制正文，按实际字节数生成 Content-Length。

### Phase 6：HTTP/1.1 Keep-Alive

- 支持同一 TCP 连接上的多轮请求，按 Connection 头决定复用或关闭。
- 保留后续请求的缓冲数据，按顺序处理连续请求和分段请求。
- 非法请求返回 400 并关闭连接。

### Phase 7：配置、日志、超时与优雅关闭

- 支持端口、静态文件根目录、worker 数量和 IO 空闲超时配置。
- 提供 INFO、WARN、ERROR 日志，整行输出到标准错误。
- 根据实际收发进展回收 IO 空闲连接，worker 处理期间不因 IO 空闲而回收。
- 通过 signalfd 响应 SIGINT/SIGTERM，停止接收新请求并为当前工作提供 5 秒网络收尾窗口。

## 构建与运行

需要支持 C++17 的编译器、CMake 3.20 及以上版本，以及 Linux / WSL 环境。
在项目根目录执行：

```bash
set -o pipefail
cmake -S . -B build 2>&1 | tee build.log &&
cmake --build build -j2 2>&1 | tee -a build.log
```

构建失败时先检查 build.log。构建成功后启动服务器：

```bash
./build/mini_http_server
```

默认监听 `127.0.0.1:8081`，另开一个终端访问：

```bash
curl --noproxy '*' -i http://127.0.0.1:8081/
```

自定义启动参数：

```bash
./build/mini_http_server --port 8082 --root ./www --threads 4 --idle-timeout 15
```

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--port PORT` | `8081` | 监听端口，范围 1～65535 |
| `--root PATH` | `www` | 静态文件根目录，相对路径基于启动工作目录 |
| `--threads N` | `2` | worker 线程数，正整数 |
| `--idle-timeout SECONDS` | `30` | IO 空闲超时秒数，正整数 |
| `--help` | — | 显示帮助 |

按 Ctrl+C 或向服务器进程发送 SIGTERM 开始优雅关闭。
5 秒为网络收尾窗口，进程退出还需要等待 worker 结束。

## 测试

项目使用 CTest 管理 11 项自动测试，覆盖 HTTP 解析与响应、Keep-Alive、
静态文件路径、连接状态、事件循环、信号资源、日志和启动配置。

```bash
set -o pipefail
ctest --test-dir build --output-on-failure 2>&1 | tee -a build.log
```

测试失败时先检查 build.log。`tests/demos/` 中保留独立示例，不注册到 CTest。
