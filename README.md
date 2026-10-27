# Mini_HTTP

## 项目简介
该 Mini_HTTP 是一个基于 C++17 和 Linux Socket API 的 HTTP 服务器学习项目
在 WSL / Ubuntu 的环境下开发

项目从阻塞 Socket 开始，逐步实现 HTTP 请求解析、响应生成，
再使用非阻塞读写和 epoll，让一个线程同时管理多个客户端连接。

当前已完成 Phase 1～Phase 4。正式服务器采用单线程、LT 模式的
epoll 事件循环，每个连接处理一次请求，发送响应后关闭连接。将
server中的功能进行了职责划分，使成为一个完整的单线程Reactor。

## 当前功能与进度

### Phase 1：最小 HTTP 服务器

- 使用 socket、bind、listen、accept 建立 TCP 服务。
- 接收客户端请求，发送固定的 HTTP 200 响应。
- 完成一次请求、响应和关闭连接的完整流程。

### Phase 2：HTTP 请求解析与响应生成

- 使用 HttpRequest 保存解析后的请求信息。
- 使用 HttpParser 解析请求行、Headers 和 Content-Length 指定的定长 Body。
- 区分请求完整、需要更多数据和请求非法三种情况。
- 使用 HttpResponse 生成状态行、响应头和响应正文。
- 接入请求处理逻辑，支持 200、400、404、405 响应。

### Phase 3：非阻塞 Socket 与 epoll

- 使用 epoll_create1、epoll_ctl、epoll_wait 管理连接事件。
- 为监听 Socket 设置 O_NONBLOCK。
- 客户端 recv/send 使用 MSG_DONTWAIT，避免等待数据或发送空间时阻塞线程。
- 使用 unordered_map 保存每个客户端的连接状态。
- 在 ConnectionState 中保存请求缓冲区、响应缓冲区和已发送字节数。
- 请求分段到达时保留已有数据，等待后续可读事件。
- 响应准备好后切换到 EPOLLOUT，等待可写事件再发送。
- 发生部分发送时保存进度，后续从未发送的位置继续。
- 遇到 EINTR 重试，遇到 EAGAIN/EWOULDBLOCK 保留连接并返回事件循环。
- 连接结束或发生 I/O 错误后，移除 epoll 监听并释放连接。
- 记录 EPOLLHUP、EPOLLERR 事件，并根据实际收发结果处理连接。

### Phase 4：Reactor 与 Thread_pool

- 拆分服务器职责：用 Server、 Event_loop、 Acceptor、 Connection分别承担服务器协调、就绪事件分发、接受连接和连续读写职责
- 实现 ThreadPool，使用任务队列、互斥锁和条件变量调度工作线程
- IO 线程负责客户端 Socket 收发、epoll 事件管理和连接生命周期;
  worker 负责完整请求的 handle_request 调用以及响应序列化。
- 使用移动捕获传递请求和响应数据， worker 不直接操作 Connection。
- 通过 EventLoop::post、线程安全任务队列和 eventfd 唤醒机制。
  将业务结果交回 IO 线程处理。
- IO 回调使用 fd 和连接 id 核对结果归属， 再保存响应并关注 EPOLLOUT。
- 连接关闭后允许在途任务完成，回调发现连接失效时丢弃结果。
- 使用 RAII 明确文件描述符所有权; 线程池析构时等待工作线程退出。

## 构建与运行

在 WSL / Ubuntu 的项目根目录执行:

```bash
cmake -S . -B build
cmake --build build
./build/mini_http_server
```

服务器默认监听 8081 端口。需要另开一个终端进行访问:
```bash
curl --noproxy '*' -i http://127.0.0.1:8081/
```

## 验证情况

- 基础 HTTP 阶段已经验证 200、400、404、405 响应、
  Content-Length、正文以及响应后关闭连接
- 已验证请求分段到达时其他连接仍能正常响应;
  通过独立发送实验验证部分发送、 WouldBlock 和 EPOLLOUT 续传。
- post 实验确认业务由 worker 执行，回调由 IO 线程执行。
- 线程池接入之后进行了正常请求、处理中客户端断开、迟到结果丢弃以及后续请求处理实验。

项目包含 HTTP Parser、Response 和请求处理逻辑三组单元测试:

```bash
ctest --test-dir build --output-on-failure
```