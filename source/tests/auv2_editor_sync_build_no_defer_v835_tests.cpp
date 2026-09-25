// Regression guard for the Logic "AUv2 editor hangs on open" bug.
//
// The v826-v834 deferred AUv2 out-of-process editor bootstrap installed a black
// placeholder and rebuilt the real editor only after a dispatch_after delay
// (ArpSIDAuv2OOPEditorBuildDelaySeconds, which had been bumped to 7.5s). If the host
// window / first-paint fence never settled, -_finishDeferredAuv2EditorBuild_v831_
// rescheduled that same delay forever, leaving a permanently black editor. Either way
// Logic appeared to hang for seconds (or indefinitely) when opening the AUv2 editor.
//
// The fix restores the known-good synchronous behavior: the editor is built SYNCHRONOUSLY
// in -loadView. This test fails if anyone re-enables the deferral, re-arms the delay,
// or removes the synchronous build, so the hang cannot silently return.
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!f) {
        std::cerr << "missing file: " << rel << "\n";
        std::abort();
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

static std::string sliceBetween(const std::string& s,
                                const std::string& begin,
                                const std::string& end) {
    const std::size_t b = s.find(begin);
    require(b != std::string::npos, "slice begin marker missing");
    const std::size_t e = s.find(end, b + begin.size());
    require(e != std::string::npos, "slice end marker missing");
    return s.substr(b, e - b);
}

int main() {
    const std::string gui = readFile("source/au3/ArpSIDViewController.mm");

    // 1. The deferral decision must be hard-disabled at its single source. This is the only
    //    site that ever sets _deferAuv2OOPEditorBuild_v831 to YES, so its return value gates
    //    the entire defer/placeholder/fence machine.
    const std::string shouldDefer = sliceBetween(
        gui,
        "static BOOL ArpSIDShouldDeferAuv2OOPBootstrap_v831(void) {",
        "#endif");
    require(shouldDefer.find("return NO;") != std::string::npos,
            "ArpSIDShouldDeferAuv2OOPBootstrap_v831 must return NO so the AUv2 editor never defers");
    require(shouldDefer.find("AUHostingService") == std::string::npos,
            "AUv2 editor deferral must not be re-enabled by AUHostingServiceXPC process detection");
    require(shouldDefer.find("return YES") == std::string::npos,
            "AUv2 editor deferral must never return YES");

    // 2. The deferred-build delay landmine must stay neutered. 7.5s (or any nonzero) on the
    //    now-unreachable scheduler could re-stall the editor if deferral were ever re-enabled.
    require(gui.find("ArpSIDAuv2OOPEditorBuildDelaySeconds_v834 = 0.0") != std::string::npos,
            "AUv2 deferred-editor build delay must be neutered to 0.0");
    require(gui.find("ArpSIDAuv2OOPEditorBuildDelaySeconds_v834 = 7.5") == std::string::npos,
            "AUv2 deferred-editor build delay must not carry the 7.5s hang value");

    // 3. The first-paint fence must be pre-released at init so -_showTab shows a restored
    //    heavy tab immediately instead of queuing it behind a paint event the synchronous
    //    path never produces.
    require(gui.find("_firstEditorPaintCompleted_v826 = YES;") != std::string::npos,
            "First-paint fence must be pre-released (=YES) at init for synchronous AUv2 build");

    // 4. -loadView must still build the editor synchronously.
    const std::string loadView = sliceBetween(
        gui,
        "-(void)loadView{",
        "-(void)_registerCoreNotificationObservers_v322_{");
    require(loadView.find("[self _buildUI]") != std::string::npos &&
                loadView.find("_built=YES") != std::string::npos,
            "-loadView must build the AUv2 editor synchronously");

    std::cout << "auv2_editor_sync_build_no_defer_v835_tests: PASS\n";
    return 0;
}
