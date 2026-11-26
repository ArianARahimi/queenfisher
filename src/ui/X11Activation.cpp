#include "X11Activation.h"

#include <QGuiApplication>
#include <QWindow>

#include <xcb/xcb.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {
template <typename T> using Reply = std::unique_ptr<T, decltype(&std::free)>;

xcb_atom_t atom(xcb_connection_t *c, const char *name)
{
    const Reply<xcb_intern_atom_reply_t> reply(
        xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, std::strlen(name), name), nullptr), &std::free);
    return reply ? reply->atom : xcb_atom_t{XCB_ATOM_NONE};
}

// "..._TIME12345" is the X server time of the click that launched it; 0 when there's none.
xcb_timestamp_t launchTime(const QByteArray &startupId)
{
    const qsizetype at = startupId.lastIndexOf("_TIME");
    if (at < 0)
        return 0;
    bool ok = false;
    const uint time = startupId.mid(at + 5).toUInt(&ok);
    return ok ? time : 0;
}

// Qt keeps the window's user time on a helper window when it has one.
xcb_window_t userTimeWindow(xcb_connection_t *c, xcb_window_t window)
{
    const xcb_get_property_cookie_t cookie = xcb_get_property(c, 0, window, atom(c, "_NET_WM_USER_TIME_WINDOW"), XCB_ATOM_WINDOW, 0, 1);
    const Reply<xcb_get_property_reply_t> reply(xcb_get_property_reply(c, cookie, nullptr), &std::free);
    if (reply && reply->format == 32 && xcb_get_property_value_length(reply.get()) == sizeof(xcb_window_t))
        return *static_cast<const xcb_window_t *>(xcb_get_property_value(reply.get()));
    return window;
}

void sendToRoot(xcb_connection_t *c, xcb_window_t root, uint32_t mask, const xcb_client_message_event_t &event)
{
    xcb_send_event(c, 0, root, mask, reinterpret_cast<const char *>(&event));
}

void activate(xcb_connection_t *c, xcb_window_t root, xcb_window_t window, xcb_timestamp_t time)
{
    const uint32_t userTime = time;
    xcb_change_property(
        c, XCB_PROP_MODE_REPLACE, userTimeWindow(c, window), atom(c, "_NET_WM_USER_TIME"), XCB_ATOM_CARDINAL, 32, 1, &userTime);
    xcb_client_message_event_t event{};
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = window;
    event.type = atom(c, "_NET_ACTIVE_WINDOW");
    event.data.data32[0] = 1;   // asked by an application
    event.data.data32[1] = time;
    sendToRoot(c, root, XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, event);
}

// Startup notification spec: the text goes in 20-byte pieces, its closing NUL included.
void endLaunch(xcb_connection_t *c, xcb_window_t root, xcb_window_t window, const QByteArray &startupId)
{
    constexpr qsizetype piece = 20;
    const QByteArray text = "remove: ID=" + startupId;
    const qsizetype length = text.size() + 1;
    xcb_client_message_event_t event{};
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 8;
    event.window = window;
    event.type = atom(c, "_NET_STARTUP_INFO_BEGIN");
    const xcb_atom_t more = atom(c, "_NET_STARTUP_INFO");
    for (qsizetype sent = 0; sent < length; sent += piece) {
        std::memset(event.data.data8, 0, piece);
        std::memcpy(event.data.data8, text.constData() + sent, std::min(piece, length - sent));
        sendToRoot(c, root, XCB_EVENT_MASK_PROPERTY_CHANGE, event);
        event.type = more;
    }
}
}

void X11Activation::bringToFront(QWindow *window, const QByteArray &startupId)
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11 || !window || startupId.isEmpty())
        return;
    xcb_connection_t *c = x11->connection();
    const auto id = static_cast<xcb_window_t>(window->winId());
    const xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;
    if (const xcb_timestamp_t time = launchTime(startupId))
        activate(c, root, id, time);
    endLaunch(c, root, id, startupId);
    xcb_flush(c);
}
