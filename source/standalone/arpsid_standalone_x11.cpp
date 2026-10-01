// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID standalone app: Linux window (X11 / XWayland).
//
// A top-level X11 window (xcb) with VSTGUI's X11 frame embedded in it, and a
// poll() run loop that serves VSTGUI's file descriptors and timers (VSTGUI
// has no event loop of its own on Linux). Resizing keeps the content's
// aspect ratio and zooms the frame; closing the window (or SIGINT / SIGTERM)
// ends the loop.

#include "standalone/arpsid_standalone_app.h"
#include "standalone/arpsid_standalone_platform.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/vstguiinit.h"

#include <xcb/xcb.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <string>
#include <vector>

namespace ArpSID::Standalone {

namespace {

std::atomic<bool> gSignalQuit{false};

void onSignal(int) { gSignalQuit.store(true); }

// VSTGUI's run loop: file descriptors and timers served by poll().
class PollRunLoop final : public VSTGUI::IRunLoop, public VSTGUI::AtomicReferenceCounted {
public:
    bool registerEventHandler(int fd, VSTGUI::IEventHandler* h) override {
        if (fd < 0 || !h) return false;
        fds_.push_back({fd, h});
        return true;
    }
    bool unregisterEventHandler(VSTGUI::IEventHandler* h) override {
        const auto it = std::find_if(fds_.begin(), fds_.end(), [h](const Fd& f) { return f.handler == h; });
        if (it == fds_.end()) return false;
        fds_.erase(it);
        return true;
    }
    bool registerTimer(uint64_t intervalMs, VSTGUI::ITimerHandler* h) override {
        if (!h) return false;
        timers_.push_back({std::max<uint64_t>(intervalMs, 1), h, Clock::now() + std::chrono::milliseconds(intervalMs)});
        return true;
    }
    bool unregisterTimer(VSTGUI::ITimerHandler* h) override {
        const auto it = std::find_if(timers_.begin(), timers_.end(), [h](const Timer& t) { return t.handler == h; });
        if (it == timers_.end()) return false;
        timers_.erase(it);
        return true;
    }

    // One pass: wait for an fd (also <ownFd>) or the next timer, then serve.
    // Returns true when <ownFd> is readable.
    bool runOnce(int ownFd) {
        const auto now = Clock::now();
        int timeout = 50;
        for (const Timer& t : timers_) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t.due - now).count();
            timeout = static_cast<int>(std::clamp<long long>(ms, 0, timeout));
        }
        std::vector<pollfd> pfds;
        pfds.push_back({ownFd, POLLIN, 0});
        const std::vector<Fd> fds = fds_;
        for (const Fd& f : fds) pfds.push_back({f.fd, POLLIN, 0});
        const int n = poll(pfds.data(), static_cast<nfds_t>(pfds.size()), timeout);
        if (n > 0) {
            for (std::size_t i = 1; i < pfds.size(); ++i) {
                if (!(pfds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
                const Fd& f = fds[i - 1];
                // Still registered (an earlier handler may have removed it)?
                if (std::any_of(fds_.begin(), fds_.end(), [&](const Fd& g) { return g.handler == f.handler; }))
                    f.handler->onEvent();
            }
        }
        const auto after = Clock::now();
        const std::vector<Timer> timers = timers_;
        for (const Timer& t : timers) {
            if (t.due > after) continue;
            auto it = std::find_if(timers_.begin(), timers_.end(), [&](const Timer& u) { return u.handler == t.handler; });
            if (it == timers_.end()) continue;
            it->due = after + std::chrono::milliseconds(it->intervalMs);
            t.handler->onTimer();
        }
        return n > 0 && (pfds[0].revents & POLLIN);
    }

private:
    using Clock = std::chrono::steady_clock;
    struct Fd { int fd; VSTGUI::IEventHandler* handler; };
    struct Timer { uint64_t intervalMs; VSTGUI::ITimerHandler* handler; Clock::time_point due; };
    std::vector<Fd> fds_;
    std::vector<Timer> timers_;
};

xcb_atom_t atom(xcb_connection_t* c, const char* name) {
    xcb_intern_atom_reply_t* r =
        xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, static_cast<uint16_t>(std::strlen(name)), name), nullptr);
    const xcb_atom_t a = r ? r->atom : static_cast<xcb_atom_t>(XCB_ATOM_NONE);
    std::free(r);
    return a;
}

// WM_NORMAL_HINTS (ICCCM): minimum size and the fixed aspect ratio.
void setSizeHints(xcb_connection_t* c, xcb_window_t w, int width, int height) {
    uint32_t hints[18] = {};
    constexpr uint32_t kPMinSize = 1u << 4, kPAspect = 1u << 7;
    hints[0] = kPMinSize | kPAspect;
    hints[5] = static_cast<uint32_t>(width / 2);  // min width
    hints[6] = static_cast<uint32_t>(height / 2); // min height
    hints[11] = static_cast<uint32_t>(width);     // min aspect num / den
    hints[12] = static_cast<uint32_t>(height);
    hints[13] = static_cast<uint32_t>(width);     // max aspect num / den
    hints[14] = static_cast<uint32_t>(height);
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, w, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32, 18, hints);
}

void setTitle(xcb_connection_t* c, xcb_window_t w, const std::string& title) {
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, w, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                        static_cast<uint32_t>(title.size()), title.data());
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, w, atom(c, "_NET_WM_NAME"), atom(c, "UTF8_STRING"), 8,
                        static_cast<uint32_t>(title.size()), title.data());
}

xcb_window_t firstChild(xcb_connection_t* c, xcb_window_t w) {
    xcb_query_tree_reply_t* r = xcb_query_tree_reply(c, xcb_query_tree(c, w), nullptr);
    xcb_window_t child = XCB_WINDOW_NONE;
    if (r && xcb_query_tree_children_length(r) > 0) child = xcb_query_tree_children(r)[0];
    std::free(r);
    return child;
}

} // namespace

int runWindow(App& app) {
    xcb_connection_t* c = xcb_connect(nullptr, nullptr);
    if (!c || xcb_connection_has_error(c)) {
        std::fprintf(stderr, "ArpSID: cannot open the X display (is DISPLAY set?)\n");
        if (c) xcb_disconnect(c);
        return 1;
    }
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    const double cw = App::contentWidth(), ch = App::contentHeight();
    // Fit the saved size on the screen.
    double zoom = app.zoom();
    zoom = std::min(zoom, std::min(screen->width_in_pixels * 0.95 / cw, screen->height_in_pixels * 0.9 / ch));
    zoom = std::max(zoom, 0.5);
    int width = static_cast<int>(std::lround(cw * zoom)), height = static_cast<int>(std::lround(ch * zoom));

    const xcb_window_t win = xcb_generate_id(c);
    const uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    const uint32_t values[2] = {screen->black_pixel,
                                XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_FOCUS_CHANGE};
    xcb_create_window(c, XCB_COPY_FROM_PARENT, win, screen->root, 0, 0, static_cast<uint16_t>(width),
                      static_cast<uint16_t>(height), 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, mask,
                      values);
    const xcb_atom_t wmProtocols = atom(c, "WM_PROTOCOLS"), wmDelete = atom(c, "WM_DELETE_WINDOW");
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, win, wmProtocols, XCB_ATOM_ATOM, 32, 1, &wmDelete);
    const char wmClass[] = "arpsid\0ArpSID";
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof(wmClass), wmClass);
    setSizeHints(c, win, static_cast<int>(cw), static_cast<int>(ch));
    std::string title = app.windowTitle();
    setTitle(c, win, title);
    xcb_map_window(c, win);
    xcb_flush(c);

    // VSTGUI: the run loop for its X connection and timers, then the frame.
    VSTGUI::init(nullptr);
    auto loop = VSTGUI::makeOwned<PollRunLoop>();
    if (auto* lf = VSTGUI::getPlatformFactory().asLinuxFactory()) lf->setRunLoop(loop);
    VSTGUI::X11::FrameConfig config;
    config.runLoop = loop;
    auto* frame = new VSTGUI::CFrame(VSTGUI::CRect(0, 0, cw, ch), nullptr);
    frame->setTransparency(false);
    if (!frame->open(reinterpret_cast<void*>(static_cast<uintptr_t>(win)), VSTGUI::PlatformType::kX11EmbedWindowID,
                     &config)) {
        std::fprintf(stderr, "ArpSID: cannot open the editor frame\n");
        frame->forget();
        xcb_destroy_window(c, win);
        xcb_disconnect(c);
        VSTGUI::exit();
        return 1;
    }
    app.attach(frame);
    frame->setZoom(zoom);

    bool quit = false;
    app.requestQuit = [&quit]() { quit = true; };
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    const int xfd = xcb_get_file_descriptor(c);
    auto lastTitle = std::chrono::steady_clock::now();
    while (!quit && !gSignalQuit.load()) {
        loop->runOnce(xfd);
        while (xcb_generic_event_t* ev = xcb_poll_for_event(c)) {
            switch (ev->response_type & 0x7F) {
                case XCB_CONFIGURE_NOTIFY: {
                    auto* e = reinterpret_cast<xcb_configure_notify_event_t*>(ev);
                    if (e->window == win && (e->width != width || e->height != height)) {
                        width = e->width;
                        height = e->height;
                        zoom = std::clamp(std::min(width / cw, height / ch), 0.5, 3.0);
                        frame->setZoom(zoom);
                        app.setZoom(zoom);
                    }
                    break;
                }
                case XCB_FOCUS_IN: {
                    // Keys go to VSTGUI's child window (the editor's keyboard).
                    const xcb_window_t child = firstChild(c, win);
                    if (child != XCB_WINDOW_NONE)
                        xcb_set_input_focus(c, XCB_INPUT_FOCUS_PARENT, child, XCB_CURRENT_TIME);
                    xcb_flush(c);
                    break;
                }
                case XCB_CLIENT_MESSAGE: {
                    auto* e = reinterpret_cast<xcb_client_message_event_t*>(ev);
                    if (e->type == wmProtocols && e->data.data32[0] == wmDelete) quit = true;
                    break;
                }
                default:
                    break;
            }
            std::free(ev);
        }
        if (xcb_connection_has_error(c)) break;
        const auto now = std::chrono::steady_clock::now();
        if (now - lastTitle > std::chrono::milliseconds(500)) {
            lastTitle = now;
            const std::string t = app.windowTitle();
            if (t != title) {
                title = t;
                setTitle(c, win, title);
                xcb_flush(c);
            }
        }
    }

    app.requestQuit = nullptr;
    app.detach();
    frame->forget();
    xcb_destroy_window(c, win);
    xcb_disconnect(c);
    if (auto* lf = VSTGUI::getPlatformFactory().asLinuxFactory()) lf->setRunLoop(nullptr);
    VSTGUI::exit();
    return 0;
}

} // namespace ArpSID::Standalone
