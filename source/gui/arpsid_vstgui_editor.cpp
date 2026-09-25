// ArpSID — VST3 native Cocoa view host.
//
// The canonical ArpSIDViewController is the only GUI implementation. The old
// hand-built VSTGUI fallback duplicated a small, stale subset of controls and
// created a second layout/automation authority.

#include "arpsid_vstgui_editor.h"

#include <algorithm>

namespace {
constexpr Steinberg::int32 kDefaultWidth = 720;
constexpr Steinberg::int32 kDefaultHeight = 520;
constexpr Steinberg::int32 kMaximumWidth = 3200;
constexpr Steinberg::int32 kMaximumHeight = 1880;
}

ArpSIDVSTGUIEditor::ArpSIDVSTGUIEditor(void* controllerPtr)
    : Steinberg::Vst::EditorView(
          reinterpret_cast<Steinberg::Vst::EditController*>(controllerPtr)) {
    setRect(Steinberg::ViewRect(0, 0, kDefaultWidth, kDefaultHeight));
}

ArpSIDVSTGUIEditor::~ArpSIDVSTGUIEditor() {
    closeNativeView_();
}

Steinberg::tresult PLUGIN_API
ArpSIDVSTGUIEditor::isPlatformTypeSupported(Steinberg::FIDString type) {
#if SMTG_OS_MACOS
    return (type && Steinberg::FIDStringsEqual(type, Steinberg::kPlatformTypeNSView))
        ? Steinberg::kResultTrue
        : Steinberg::kResultFalse;
#else
    (void)type;
    return Steinberg::kResultFalse;
#endif
}

Steinberg::tresult PLUGIN_API
ArpSIDVSTGUIEditor::attached(void* parent, Steinberg::FIDString type) {
#if SMTG_OS_MACOS
    if (!parent || isAttached() ||
        isPlatformTypeSupported(type) != Steinberg::kResultTrue)
        return Steinberg::kResultFalse;

    nativeCocoaHandle_ =
        ArpSID::arpsidOpenRichCocoaEditor(parent, static_cast<void*>(getController()));
    if (!nativeCocoaHandle_) return Steinberg::kResultFalse;
    systemWindow = parent;
    attachedToParent();
    return Steinberg::kResultTrue;
#else
    (void)parent;
    (void)type;
    return Steinberg::kResultFalse;
#endif
}

Steinberg::tresult PLUGIN_API ArpSIDVSTGUIEditor::removed() {
    if (!isAttached() && !nativeCocoaHandle_) return Steinberg::kResultFalse;
    removedFromParent();
    closeNativeView_();
    systemWindow = nullptr;
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API ArpSIDVSTGUIEditor::getSize(Steinberg::ViewRect* size) {
    if (!size) return Steinberg::kInvalidArgument;
    *size = getRect();
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API
ArpSIDVSTGUIEditor::onSize(Steinberg::ViewRect* newSize) {
    if (!newSize) return Steinberg::kInvalidArgument;
    checkSizeConstraint(newSize);
    setRect(*newSize);
    if (nativeCocoaHandle_) {
        ArpSID::arpsidResizeRichCocoaEditor(
            nativeCocoaHandle_,
            static_cast<double>(newSize->right - newSize->left),
            static_cast<double>(newSize->bottom - newSize->top));
    }
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API ArpSIDVSTGUIEditor::canResize() {
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API
ArpSIDVSTGUIEditor::checkSizeConstraint(Steinberg::ViewRect* rect) {
    if (!rect) return Steinberg::kInvalidArgument;
    const Steinberg::int32 width = std::clamp(
        rect->right - rect->left, kDefaultWidth, kMaximumWidth);
    const Steinberg::int32 height = std::clamp(
        rect->bottom - rect->top, kDefaultHeight, kMaximumHeight);
    rect->right = rect->left + width;
    rect->bottom = rect->top + height;
    return Steinberg::kResultTrue;
}

void ArpSIDVSTGUIEditor::closeNativeView_() noexcept {
    if (!nativeCocoaHandle_) return;
    ArpSID::arpsidCloseRichCocoaEditor(nativeCocoaHandle_);
    nativeCocoaHandle_ = nullptr;
}
