#include "llm/HttpTransport.h"
#include "core/Errors.h"
#include <curl/curl.h>
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
      // 诊断（无条件）：记录每块到达的时间(ms)+字节数，判断 curl 是否真在流式。
      {
        static auto t0 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
        if (std::FILE *f = std::fopen("/tmp/aicoder_stream.log", "a"))
        {
          std::fprintf(f, "[+%5lldms] chunk %zu bytes\n",
                       static_cast<long long>(ms), chunk.size());
          std::fclose(f);
        }
      }
      ctx->full_body->append(chunk);
      (*ctx->on_chunk)(chunk);
      return size * nmemb;
    }
  }

  HttpTransport::HttpTransport()
  {
    std::call_once(g_curlInit, []
                   { curl_global_init(CURL_GLOBAL_DEFAULT); });
  }

  HttpResponse HttpTransport::post(const std::string &url,
                                   const std::string &body,
                                   const std::vector<std::string> &headers)
  {
    CURL *curl = curl_easy_init();
    if (!curl)
      throw LlmError("curl 初始化失败");

    std::string response;
    curl_slist *hdrs = nullptr;
    for (const auto &h : headers)
      hdrs = curl_slist_append(hdrs, h.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
      throw LlmError(std::string("HTTP 传输失败: ") + curl_easy_strerror(rc));
    return HttpResponse{status, response};
  }

  HttpResponse HttpTransport::postStream(const std::string &url,
                                         const std::string &body,
                                         const std::vector<std::string> &headers,
                                         const ChunkCallback &onChunk)
  {
    // 诊断（无条件）：记录 postStream 是否被调用（确认走的是流式路径）。
    if (std::FILE *f = std::fopen("/tmp/aicoder_stream.log", "a"))
    {
      std::fprintf(f, "=== postStream ENTER url=%s ===\n", url.c_str());
      std::fclose(f);
    }

    CURL *curl = curl_easy_init();
    if (!curl)
      throw LlmError("curl 初始化失败");

    std::string full_body;
    StreamCtx ctx{&full_body, &onChunk};
    curl_slist *hdrs = nullptr;
    for (const auto &h : headers)
      hdrs = curl_slist_append(hdrs, h.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streamWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
      throw LlmError(std::string("HTTP 传输失败: ") + curl_easy_strerror(rc));
    return HttpResponse{status, full_body};
  }

}
