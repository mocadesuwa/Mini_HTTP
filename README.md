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

- 拆分服务器职责：用 Server、 Event_loop、 Acceptor、 Connection承担相应任务
- 明确事件分发与 HTTP 处理的边界
- 明确资源所有权与连接生命周期
- 确认重构后原有行为正常
## 构建与运行

## 验证情况