# HTTP Client HTTP客户端组件

> jiejieTop开源库,对象池+拦截器模式,支持GET/POST/chunked/重定向/Keep-Alive,http_parser状态机解析响应

## 快速定位

**核心文件:**
- `httpclient/http_interceptor.c:617-670` - http_interceptor_process() 状态机流程(init→connect→request→headers→body→release)
- `httpclient/httpclient.c:136-172` - _http_client_handle() 客户端调用入口(lease/设置参数/interceptor处理)
- `httpclient/httpclient.c:98-112` - _http_client_pool_init() 对象池初始化(free_list/used_list)
- `httpclient/http_interceptor.c:21-63` - _http_read/write_buffer() 网络IO(network_read/write+超时控制)
- `httpclient/http_interceptor.c:120-167` - _http_on_header_field/value() http_parser回调
- `libhttp/http_parser.c` - joyent/http-parser状态机(RFC 7230解析器)

**关键结构:**
- `httpclient.h:41-52` - http_client_t(list/interceptor/connect_params/method/event/keep_alive)
- `http_interceptor.h` - http_interceptor_t(network/request/response/parser/buffer/status/flags)
- `http_interceptor.h` - http_interceptor_status_t状态(invalid/init/connect/request/headers_complete/response_body/release)

**关键宏:**
- `http_defconfig.h` - HTTP_CLIENT_POOL_SIZE对象池大小
- `http_defconfig.h` - HTTP_BUFFER_SIZE默认缓冲区大小

## 核心API

**客户端管理:**
- `http_client_lease()` - 从对象池获取http_client_t(free_list→used_list)
- `http_client_release(c)` - 归还http_client_t到对象池
- `_http_client_pool_init()` - 初始化对象池(预分配HTTP_CLIENT_POOL_SIZE个)

**请求发送:**
- `http_client_get(url, cb)` - GET请求
- `http_client_post(url, post_buf, cb)` - POST请求
- `http_client_set_header(c, key, value)` - 设置请求头

**拦截器状态机(http_interceptor.c):**
- `http_interceptor_process()` - 状态机主流程(init→connect→request→fetch→release)
- `http_interceptor_connect()` - TCP/TLS连接
- `http_interceptor_request()` - 发送HTTP请求
- `http_interceptor_fetch_headers()` - 接收解析响应头
- `http_interceptor_fetch_data()` - 接收响应体(支持chunked)

**事件回调:**
- `http_event_cb_t.on_headers(owner, hdritems)` - 响应头完成回调
- `http_event_cb_t.on_data(owner, data, len)` - 响应体数据回调
- `http_event_cb_t.on_complete(owner)` - 请求完成回调

## AI理解要点

1. **jiejieTop开源**: 基于https://github.com/jiejieTop/http-client,轻量级HTTP库 `httpclient.c:1-7`

2. **对象池模式**: 预分配HTTP_CLIENT_POOL_SIZE个http_client_t,free_list/used_list管理 `httpclient.c:98-112`

3. **拦截器模式**: http_interceptor_t封装请求/响应处理,状态机驱动 `http_interceptor.c:617-670`

4. **Fall-through switch**: interceptor_process无break,单次调用完成整个请求 `http_interceptor.c:635-666`

5. **joyent/http-parser**: 基于Node.js的RFC 7230解析器,高性能状态机 `libhttp/http_parser.c`

6. **事件回调**: http_event_t注册on_headers/on_data/on_complete回调 `httpclient.c:29-50`

7. **Keep-Alive**: Connection: keep-alive头部,复用TCP连接 `http_interceptor.c:672-676`

8. **重定向处理**: Location头部检测,设置redirects标志 `http_interceptor.c:125-126`

9. **平台抽象**: platform/FreeRTOS适配层,network_t/platform_timer_t跨平台 `platform/FreeRTOS/`

10. **HTTPS支持**: network_set_ca()设置CA证书,底层mbedtls `http_interceptor.c:95-97`

