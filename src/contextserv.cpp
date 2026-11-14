/*
 * SPDX-FileCopyrightText: 2026~2026 gapul
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "contextserv.h"
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>
#include <fcitx-utils/log.h>
#include <fcitx-utils/stringutils.h>

namespace {

// How long one request may block fcitx5. Zenzai answers in well under 100 ms
// on a laptop CPU. A request that times out is not sent again, and the server
// is left alone for kSuspendSeconds, so a hung server costs one pause rather
// than one per keystroke (libskk may look a reading up twice in a row).
constexpr int kTimeoutSeconds = 3;
constexpr auto kSuspendSeconds = std::chrono::seconds(30);

std::string percentEncode(std::string_view text) {
    static const char hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
            c == '~') {
            result.push_back(static_cast<char>(c));
        } else {
            result.push_back('%');
            result.push_back(hex[c >> 4]);
            result.push_back(hex[c & 0xf]);
        }
    }
    return result;
}

// Splits "/a/b;annotation/" into its entries, as libskk's split_candidates.
std::vector<std::string> splitEntries(std::string_view body) {
    body = fcitx::stringutils::trimView(body);
    if (body.size() < 2 || body.front() != '/') {
        return {};
    }
    body.remove_prefix(1);
    if (body.back() == '/') {
        body.remove_suffix(1);
    }
    std::vector<std::string> entries;
    for (auto &entry : fcitx::stringutils::split(body, "/")) {
        if (!entry.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

struct ContextServImpl {
    std::string host;
    std::string port;
    fcitx::SkkContextProvider provider;
    int fd = -1;
    bool timedOut = false;
    std::chrono::steady_clock::time_point suspendedUntil;

    ~ContextServImpl() { close(); }

    void close() {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    bool connect() {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo *result = nullptr;
        if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) {
            return false;
        }
        timeval timeout{kTimeoutSeconds, 0};
        for (auto *ai = result; ai; ai = ai->ai_next) {
            int s = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC,
                           ai->ai_protocol);
            if (s < 0) {
                continue;
            }
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            if (::connect(s, ai->ai_addr, ai->ai_addrlen) == 0) {
                fd = s;
                break;
            }
            ::close(s);
        }
        freeaddrinfo(result);
        return fd >= 0;
    }

    std::optional<std::string> exchange(const std::string &request) {
        size_t written = 0;
        while (written < request.size()) {
            auto n = send(fd, request.data() + written,
                          request.size() - written, MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                return std::nullopt;
            }
            written += n;
        }
        std::string response;
        char buffer[4096];
        while (response.find('\n') == std::string::npos) {
            auto n = recv(fd, buffer, sizeof(buffer), 0);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                timedOut = true;
            }
            if (n <= 0) {
                return std::nullopt;
            }
            response.append(buffer, n);
        }
        response.resize(response.find('\n'));
        return response;
    }

    // Sends one request and returns the response line without its newline.
    // A connection that has gone away (the server restarted) is opened again
    // once, so the dictionary does not stay dead as libskk's SkkServ does.
    std::optional<std::string> request(const std::string &request) {
        if (std::chrono::steady_clock::now() < suspendedUntil) {
            return std::nullopt;
        }
        for (int attempt = 0; attempt < 2; attempt++) {
            if (fd < 0 && !connect()) {
                return std::nullopt;
            }
            timedOut = false;
            if (auto response = exchange(request)) {
                return response;
            }
            close();
            if (timedOut) {
                suspendedUntil =
                    std::chrono::steady_clock::now() + kSuspendSeconds;
                return std::nullopt;
            }
        }
        return std::nullopt;
    }
};

} // namespace

struct _FcitxSkkContextServ {
    SkkDict parent_instance;
    ContextServImpl *impl;
};

struct _FcitxSkkContextServClass {
    SkkDictClass parent_class;
};

G_DEFINE_TYPE(FcitxSkkContextServ, fcitx_skk_context_serv, SKK_TYPE_DICT)

static ContextServImpl *impl(SkkDict *self) {
    return reinterpret_cast<FcitxSkkContextServ *>(self)->impl;
}

static void fcitx_skk_context_serv_reload(SkkDict *self, GError ** /*error*/) {
    impl(self)->close();
}

static SkkCandidate **fcitx_skk_context_serv_lookup(SkkDict *self,
                                                    const gchar *midasi,
                                                    gboolean okuri,
                                                    gint *result_length) {
    auto *d = impl(self);
    std::pair<std::string, std::string> context;
    if (d->provider) {
        context = d->provider();
    }
    auto response = d->request("5" + std::string(midasi) + " " +
                               percentEncode(context.first) + " " +
                               percentEncode(context.second) + "\n");
    std::vector<std::string> entries;
    if (response && !response->empty() && response->front() == '1') {
        entries = splitEntries(std::string_view(*response).substr(1));
    }

    auto **candidates = g_new0(SkkCandidate *, entries.size() + 1);
    for (size_t i = 0; i < entries.size(); i++) {
        const auto &entry = entries[i];
        auto semicolon = entry.find(';');
        if (semicolon == std::string::npos) {
            candidates[i] =
                skk_candidate_new(midasi, okuri, entry.c_str(), nullptr, nullptr);
        } else {
            auto text = entry.substr(0, semicolon);
            auto annotation = entry.substr(semicolon + 1);
            candidates[i] = skk_candidate_new(midasi, okuri, text.c_str(),
                                              annotation.c_str(), nullptr);
        }
    }
    if (result_length) {
        *result_length = static_cast<gint>(entries.size());
    }
    return candidates;
}

static gchar **fcitx_skk_context_serv_complete(SkkDict *self,
                                               const gchar *midasi,
                                               gint *result_length) {
    auto response = impl(self)->request("4" + std::string(midasi) + " ");
    std::vector<std::string> entries;
    if (response && !response->empty() && response->front() == '1') {
        entries = splitEntries(std::string_view(*response).substr(1));
    }
    auto **completions = g_new0(gchar *, entries.size() + 1);
    for (size_t i = 0; i < entries.size(); i++) {
        completions[i] = g_strdup(entries[i].c_str());
    }
    if (result_length) {
        *result_length = static_cast<gint>(entries.size());
    }
    return completions;
}

static gboolean fcitx_skk_context_serv_get_read_only(SkkDict * /*self*/) {
    return TRUE;
}

static void fcitx_skk_context_serv_finalize(GObject *object) {
    delete reinterpret_cast<FcitxSkkContextServ *>(object)->impl;
    G_OBJECT_CLASS(fcitx_skk_context_serv_parent_class)->finalize(object);
}

static void fcitx_skk_context_serv_class_init(FcitxSkkContextServClass *klass) {
    auto *dictClass = SKK_DICT_CLASS(klass);
    dictClass->reload = fcitx_skk_context_serv_reload;
    dictClass->lookup = fcitx_skk_context_serv_lookup;
    dictClass->complete = fcitx_skk_context_serv_complete;
    dictClass->get_read_only = fcitx_skk_context_serv_get_read_only;
    G_OBJECT_CLASS(klass)->finalize = fcitx_skk_context_serv_finalize;
}

static void fcitx_skk_context_serv_init(FcitxSkkContextServ *self) {
    self->impl = new ContextServImpl;
}

FcitxSkkContextServ *
fcitx_skk_context_serv_new(const std::string &host, uint16_t port,
                           fcitx::SkkContextProvider provider) {
    auto *self = static_cast<FcitxSkkContextServ *>(
        g_object_new(FCITX_SKK_TYPE_CONTEXT_SERV, nullptr));
    self->impl->host = host;
    self->impl->port = std::to_string(port);
    self->impl->provider = std::move(provider);
    return self;
}
