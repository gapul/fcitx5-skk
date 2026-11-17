/*
 * SPDX-FileCopyrightText: 2026~2026 gapul
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#ifndef _FCITX_SKK_CONTEXTSERV_H_
#define _FCITX_SKK_CONTEXTSERV_H_

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <glib-object.h>
#include <libskk/libskk.h>

namespace fcitx {

// Returns the text before and after the cursor of the focused input context.
using SkkContextProvider =
    std::function<std::pair<std::string, std::string>()>;

// Which lookups a context server answers: okuri-nasi readings ("かがく"),
// okuri-ari ones ("つかt" for 使った), or both. The others get no candidates.
enum class SkkContextServOkuri { Nasi, Ari, All };

} // namespace fcitx

G_BEGIN_DECLS

#define FCITX_SKK_TYPE_CONTEXT_SERV (fcitx_skk_context_serv_get_type())

typedef struct _FcitxSkkContextServ FcitxSkkContextServ;
typedef struct _FcitxSkkContextServClass FcitxSkkContextServClass;

GType fcitx_skk_context_serv_get_type(void) G_GNUC_CONST;

G_END_DECLS

/*
 * An skkserv dictionary that also sends the text around the cursor.
 *
 * Lookups use the extended request "5{midasi} {left} {right}\n" of
 * azoo-key-skkserv, where left and right are percent-encoded UTF-8, and the
 * response is the same as for "1". Readings of the kind okuri excludes are
 * not sent and get no candidates. Completion uses the standard "4". Unlike
 * libskk's SkkServ, a lost connection is opened again on the next request.
 * The server must answer in UTF-8.
 */
FcitxSkkContextServ *
fcitx_skk_context_serv_new(const std::string &host, uint16_t port,
                           fcitx::SkkContextProvider provider,
                           fcitx::SkkContextServOkuri okuri);

#endif // _FCITX_SKK_CONTEXTSERV_H_
