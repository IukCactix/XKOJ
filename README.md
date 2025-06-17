> 运行项目：cmake + g++8

> chmod +x build.sh run.sh

> ./build.sh

> ./run.sh


**技术栈：**
C++17、分层架构-MVC-（表示层、控制层、业务层、数据层）、网络编程-Epoll-边缘触发、HTTP协议
设计模式：工厂模式（中间件创建和管理）、策略模式（路由匹配和处理策略）、观察者模式（事件通知机制）、RAII、单例。

**核心架构（网络层）：**
```
┌─────────────────┐
│   Main Thread   │ ← 主线程：epoll事件循环
├─────────────────┤
│  Accept Thread  │ ← 连接接受：新连接处理
├─────────────────┤
│  Thread Pool    │ ← 工作线程池：请求处理
├─────────────────┤
│ Cleanup Thread  │ ← 清理线程：超时连接回收
└─────────────────┘
```
epoll边缘触发、同步非阻塞IO、连接池管理、信号处理关闭，资源安全释放。

**HTTP处理层：**
```
Request → Parse → Route → Middleware → Handler → Response
   ↓        ↓       ↓        ↓          ↓         ↓
 Socket   请求解析  路由匹配  中间件链    业务处理   响应发送
```
流式解析：边读边解析
正则路由：支持RESTful API和参数提取
> RESTful API：基于HTTP协议的API设计风格，使用标准的HTTP方法操作资源。

**中间件架构：**
```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ CORS中间件   │ →  │ Auth中间件    │ →  │ 限流中间件   │
└──────────────┘    └──────────────┘    └──────────────┘
                            ↓
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ 静态文件     │ ←  │ 日志中间件    │ ←  │ 业务处理器    │
└──────────────┘    └──────────────┘    └──────────────┘
```

### 模块职责划分
#### core/
* Httpserver : 服务器主体，网络处理
* HttpRequest/HttpResponse : HTTP协议封装
* Middleware : 中间件基础设施
* ThreadPool : 线程池管理
#### utils/
* ConfigManager : 配置文件管理
* Logger : 日志系统
* StringUtils : 字符串处理
* CryptoUtils : 加密解密

#### modules/
* User : 用户管理
* Problem : 题目管理
* Judge : 测评系统
* Contest : 比赛系统
* Blog : 博客系统
* Monitor : 监控系统

### 技术选型？
#### 为什么选择C++17?
* 智能指针：自动内存管理，避免泄露
* std::function : 函数对象封装，支持Lambda
* std::regex : 正则表达式路由匹配
* std::thread : 标准线程库，跨平台
#### 为什么使用epoll?
* Linux原生：性能最优，支持大量并发
* 边缘触发：减少系统调用次数
* 水平+边缘：灵活的事件处理模式
#### 为什么采用线程池?
* 资源控制：避免线程过多导致调度开销
* 任务分发：请求均匀分配到工作线程
* 相应时间：避免频繁创建销毁线程

#### 中间件设计优势
```
server.use(cors_middleware);         //跨域处理
server.use(auth_middleware);         //身份认证
server.use(rate_limit_middleware);   //流量控制
server.use(logging_middleware);      //日志记录
```
### 性能优化策略
#### 内存优化
* 对象池：复用Request/Response对象
* 缓冲区管理：预分配读写缓冲区
* 字符串优化：避免不必要的拷贝
#### IO优化
* 批量读写：减少系统调用次数
* 异步处理：非阻塞IO操作
* 连接复用：Keep-Alive减少握手

#### 计算优化
* 路由缓存：热点路由结果缓存
* 正则编译：预编译路由正则表达式
* 哈希索引：快速Header查找
