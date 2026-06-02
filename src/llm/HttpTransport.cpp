#include "llm/HttpTransport.h"
#include "core/Errors.h"
#include <curl/curl.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace aicoder
{

  namespace
  {
    std::once_flag g_curlInit;

    size_t writeCb(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
      auto *out = static_cast<std::string *>(userdata);
      out->append(ptr, size * nmemb);
      return size * nmemb;
    }

    // 流式写回调的上下文：把每块字节既追加到 full_body（错误报告用），
    // 又转发给 onChunk（解析用）。
    struct StreamCtx
    {
      std::string *full_body;
      const Transport::ChunkCallback *on_chunk;
    };

    size_t streamWriteCb(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
      auto *ctx = static_cast<StreamCtx *>(userdata);
      std::string chunk(ptr, size * nmemb);
      ctx->full_body->append(chunk);
      (*ctx->on_chunk)(chunk);
      return size * nmemb;
    }

    // libcurl progress callback:每收到数据块/每秒定时被 libcurl 调用。
    // 我们借此机会检查 cancel 信号:若置 true → 返回非 0 → libcurl
    // 中止传输并把 rc 设为 CURLE_ABORTED_BY_CALLBACK。
    // 频率控制:每 200ms 才检查一次(避免空轮询)。
    int xferInfoCb(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
    {
      auto *cancel = static_cast<const std::atomic<bool> *>(clientp);
      if (!cancel)
        return 0;
      static thread_local auto last = std::chrono::steady_clock::now();
      auto now = std::chrono::steady_clock::now();
      if (now - last < std::chrono::milliseconds(200))
        return 0;
      last = now;
      return cancel->load(std::memory_order_relaxed) ? 1 : 0;
    }

    // 公共设置:URL / body / headers / write 函数(避免 post/postStream 重复)。
    // 返回设置的 curl handle(未 perform);write_data_ptr 由调用方提供。
    CURL *setupEasy(const std::string &url, const std::string &body,
                    const std::vector<std::string> &headers,
                    curl_slist *&hdrs_out,
                    size_t (*write_fn)(char *, size_t, size_t, void *),
                    void *write_data,
                    const std::atomic<bool> *cancel)
    {
      CURL *curl = curl_easy_init();
      if (!curl)
        throw LlmError("curl 初始化失败");
      hdrs_out = nullptr;
      for (const auto &h : headers)
        hdrs_out = curl_slist_append(hdrs_out, h.c_str());

      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs_out);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_fn);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, write_data);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
      // 取消机制:挂 progress callback。仅当 cancel 非空时挂(老调用零侵入)。
      if (cancel)
      {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferInfoCb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool> *>(cancel));
      }
      return curl;
    }

    // 收尾:统一 throw 文案 + 释放资源。
    [[noreturn]] void throwFromCurl(CURLcode rc)
    {
      throw LlmError(std::string("HTTP 传输失败: ") + curl_easy_strerror(rc));
    }
  }  // namespace

  HttpTransport::HttpTransport()
  {
    std::call_once(g_curlInit, []
                   { curl_global_init(CURL_GLOBAL_DEFAULT); });
  }

  HttpResponse HttpTransport::post(const std::string &url,
                                   const std::string &body,
                                   const std::vector<std::string> &headers,
                                   const std::atomic<bool> *cancel)
  {
    std::string response;
    curl_slist *hdrs = nullptr;
    CURL *curl = setupEasy(url, body, headers, hdrs, writeCb, &response, cancel);

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
      throwFromCurl(rc);
    return HttpResponse{status, response};
  }

  HttpResponse HttpTransport::postStream(const std::string &url,
                                         const std::string &body,
                                         const std::vector<std::string> &headers,
                                         const ChunkCallback &onChunk,
                                         const std::atomic<bool> *cancel)
  {
    std::string full_body;
    StreamCtx ctx{&full_body, &onChunk};
    curl_slist *hdrs = nullptr;
    CURL *curl = setupEasy(url, body, headers, hdrs, streamWriteCb, &ctx, cancel);

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
      throwFromCurl(rc);
    return HttpResponse{status, full_body};
  }

}
