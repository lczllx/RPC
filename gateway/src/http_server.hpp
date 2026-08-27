#pragma once
// =============================================================================
// http_server.hpp — HTTP/1.1 服务器（Phase 1：HTTP 接入层）
// =============================================================================
// 底层网络：dlmuduo::TcpServer（accept / epoll / IO 线程池 / Buffer / 连接管理），
// 不手写 socket/epoll，不管理线程生命周期。和 RpcServer 用的是完全相同的 dlmuduo 基础设施。
// （原基于 muduo，已迁移到自有网络库 dlmuduo；dlmuduo 的 TcpServer 自带 base loop，
//   构造函数只接收端口号，Start() 阻塞进入主事件循环，不再需要外部传入 EventLoop。）
//
// 上层解析：手写 HTTP 请求行 + 头部 + Content-Length body 的拆解逻辑。
//
// 线程模型：
//   setThreadNum(N) 控制 dlmuduo IO 线程池大小，每个连接收到的数据由 dlmuduo 回调
//   drive，解析全程在 IO 线程栈上完成——不创建额外线程、不跨线程传递请求状态、
//   不做异步派发。响应组装使用 dlmuduo::Buffer，复用 dlmuduo 的 I/O 路径。
//
// 用法（和 RpcServer 同款接口风格）：
//   lcz_gateway::HttpServer srv(8080, 4);
//   srv.setCallback([](const HttpReq& req, HttpResp* resp) { ... });
//   srv.start();  // 阻塞，直到 srv.stop() 被（信号处理器等）跨线程调用
// =============================================================================
#include "TcpServer.hpp"
#include "Connection.hpp"
#include "Buffer.hpp"
#include "CallbackTypes.hpp"
#include <string>
#include <map>
#include <cstring>
#include <strings.h> // strcasecmp
#include <functional>

namespace lcz_gateway
{

    // HTTP 请求——只存网关用到的字段，不实现完整 RFC 7230
    struct HttpReq
    {
        std::string method; // "GET" / "POST"
        std::string path;   // "/api/echo"
        std::map<std::string, std::string> headers;
        std::string body; // 按 Content-Length 读取的请求体
    };

    // HTTP 响应——回调内 fill，由 HttpServer 组装成字节流发送
    struct HttpResp
    {
        int status = 200;
        std::string status_msg; // 空则按 status 自动填 "OK"/"Not Found"/...
        std::map<std::string, std::string> headers;
        std::string body;

        void setContentType(const std::string &t) { headers["Content-Type"] = t; }
        void setBody(const std::string &b) { body = b; }
    };

    class HttpServer
    {
    public:
        using Callback = std::function<void(const HttpReq &, HttpResp *)>;

        // port: 监听端口
        // thread_num: dlmuduo IO 线程数，默认 4，和 RpcServer 一致
        HttpServer(uint16_t port, int thread_num = 4)
            : _server(port)
        {
            if (thread_num > 0)
                _server.SetThreadCnt(thread_num);
            // 消息回调绑定到本类的 onMessage，由 dlmuduo IO 线程驱动
            // （dlmuduo 的 MessageCallBack 为 (PtrConnection, Buffer*)，无 Timestamp）
            _server.SetMessageCallBack(
                [this](const PtrConnection &conn, Buffer *buf)
                {
                    onMessage(conn, buf);
                });
        }

        void setCallback(const Callback &cb) { _cb = cb; }
        void setThreadNum(int n) { _server.SetThreadCnt(n); }
        void start() { _server.Start(); } // 阻塞进入主事件循环
        void stop() { _server.Stop(); }   // 线程安全，唤醒 Start() 返回

    private:
        // 从 Buffer 读一行（去掉结尾 \r\n 或 \n）。
        // 行不完整（没有换行符）返回 false 且不消费任何字节，等下次 onMessage。
        // dlmuduo 的 FindcrLf() 返回指向 '\n' 的指针（不含行首），与 muduo findCRLF()
        // 返回 '\r' 不同，故这里单独封装以统一剥离行尾。
        static bool readLine(Buffer *buf, std::string *line)
        {
            char *lf = buf->FindcrLf();
            if (!lf)
                return false; // 行不完整，等下次 onMessage

            const char *rp = buf->GetReadPtr();
            size_t raw_len = static_cast<size_t>(lf - rp); // 到 '\n' 之前的字节数（含可能的前置 '\r'）
            size_t content_len = raw_len;
            if (content_len > 0 && rp[content_len - 1] == '\r')
                --content_len; // 去掉 '\r'

            line->assign(rp, content_len);
            buf->MoveReadoffset(static_cast<uint64_t>(raw_len + 1)); // 消费行内容 + '\n'（含 '\r'）
            return true;
        }

        // ---- HTTP 请求解析 ----
        // 在单个 onMessage 回调中完成"请求行→头部→body"的同步解析。
        // 粘包/半包由 dlmuduo Buffer 和返回语义处理：数据不够时直接 return，
        // dlmuduo 下次触发 onMessage 时 buf 中已追加新到达的字节，继续从上次中断处解析。
        // 单个请求最大 body 上限 10 MB，防止恶意大包撑爆内存。
        void onMessage(const PtrConnection &conn, Buffer *buf)
        {
            // ---- 请求行：METHOD SP PATH SP HTTP/1.x CRLF ----
            std::string request_line;
            if (!readLine(buf, &request_line))
                return; // 行不完整，等下次 onMessage

            HttpReq req;
            size_t sp1 = request_line.find(' ');
            size_t sp2 = request_line.find(' ', sp1 + 1);
            if (sp1 == std::string::npos || sp2 == std::string::npos)
            {
                conn->Shutdown(); // 格式错误，直接关连接，不回复（防御恶意扫描）
                return;
            }
            req.method = request_line.substr(0, sp1);
            req.path = request_line.substr(sp1 + 1, sp2 - sp1 - 1);

            // ---- 头部行：Key: Value CRLF ... 空行 CRLF 表示头结束 ----
            int content_length = 0;
            for (;;)
            {
                std::string line;
                if (!readLine(buf, &line))
                    return; // 不完整

                if (line.empty())
                    break; // 空行 = 头结束标志

                size_t colon = line.find(':');
                if (colon == std::string::npos)
                    continue; // 非标准行，跳过

                std::string key = line.substr(0, colon);
                std::string val = line.substr(colon + 1);
                // 去掉冒号后的前导空格
                size_t nsp = 0;
                while (nsp < val.size() && val[nsp] == ' ')
                    ++nsp;
                if (nsp > 0)
                    val = val.substr(nsp);

                req.headers[key] = val;
                // 合并不区分大小写：Content-Length / content-length 均命中
                if (strcasecmp(key.c_str(), "Content-Length") == 0)
                    content_length = std::stoi(val);
            }

            // ---- Body：按 Content-Length 精确读取 ----
            // Content-Length 是唯一支持的 body 定界方式——不实现 chunked
            // (Transfer-Encoding: chunked) 和 Connection: keep-alive，
            // 理由：API 网关的典型上游是 curl / axios / Postman / 微服务，
            // 这些客户端发 JSON 时都会带 Content-Length，chunked 仅在流式上传场景出现，
            // 网关暂不覆盖。
            if (content_length > 0)
            {
                if (content_length > 10 * 1024 * 1024)
                { // 10 MB 硬上限
                    conn->Shutdown();
                    return;
                }
                // dlmuduo Buffer 可能分包到达——ReadableBytes() < content_length
                // 时直接 return，dlmuduo 下次 onMessage 时 buf 里已有新字节
                if (buf->ReadableBytes() < static_cast<size_t>(content_length))
                    return; // 数据不完整，等下次
                req.body.assign(buf->GetReadPtr(), static_cast<size_t>(content_length));
                buf->MoveReadoffset(static_cast<uint64_t>(content_length));
            }

            // ---- 调用户回调，填充响应 ----
            HttpResp resp;
            resp.setContentType("application/json");
            if (_cb)
                _cb(req, &resp);

            // ---- 组装 HTTP 响应并发送 ----
            sendResponse(conn, resp);
        }

        // 使用 dlmuduo Buffer 组装响应，复用 dlmuduo 的 I/O 写路径。
        // 短连接模式：发完立刻 shutdown。
        // —— API 网关场景下，后端 RPC 调用已经占了连接（RpcClient 连接池），
        //    网关 HTTP 层再做 keep-alive 收益极小（同一客户端短时间内的后续请求
        //    仍需重新走鉴权/限流/路由，无法复用会话状态），徒增 TIME_WAIT 管理复杂度。
        void sendResponse(const PtrConnection &conn, const HttpResp &resp)
        {
            Buffer buf;

            buf.WritestringAndpush("HTTP/1.1 " + std::to_string(resp.status) + " " +
                                   (resp.status_msg.empty() ? statusMsg(resp.status)
                                                            : resp.status_msg) +
                                   "\r\n");

            for (const auto &[k, v] : resp.headers)
                buf.WritestringAndpush(k + ": " + v + "\r\n");

            buf.WritestringAndpush("Content-Length: " + std::to_string(resp.body.size()) + "\r\n");
            buf.WritestringAndpush("Connection: close\r\n");
            buf.WritestringAndpush("\r\n");
            buf.WritestringAndpush(resp.body);

            conn->Send(buf.GetReadPtr(), buf.ReadableBytes());
            conn->Shutdown();
        }

        static const char *statusMsg(int code)
        {
            switch (code)
            {
            case 200:
                return "OK";
            case 400:
                return "Bad Request";
            case 404:
                return "Not Found";
            case 429:
                return "Too Many Requests";
            case 500:
                return "Internal Server Error";
            case 502:
                return "Bad Gateway";
            case 503:
                return "Service Unavailable";
            default:
                return "Unknown";
            }
        }

        TcpServer _server;
        Callback _cb;
    };

} // namespace lcz_gateway
