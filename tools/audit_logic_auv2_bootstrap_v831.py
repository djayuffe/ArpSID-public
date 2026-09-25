#!/usr/bin/env python3
from pathlib import Path
import sys

src = Path("source/au3/ArpSIDViewController.mm").read_text()

def body(start_sig, end_sig=None):
    a = src.find(start_sig)
    if a < 0:
        return ""
    b = src.find(end_sig, a + 1) if end_sig else src.find("\n-(void)", a + 1)
    return "" if b < 0 else src[a:b]

errors = []

detect = body("static BOOL ArpSIDShouldDeferAuv2OOPBootstrap_v831", "@interface ArpSIDViewController")
for token in ("AUHostingService", "processName", "executablePath"):
    if token not in detect:
        errors.append("AUHostingService defer detector missing token: " + token)

load = body("-(void)loadView{", "-(void)_registerCoreNotificationObservers_v322_")
for token in ("ArpSIDShouldDeferAuv2OOPBootstrap_v831()",
              "_installAuv2BootstrapPlaceholder_v831_",
              "_scheduleDeferredAuv2EditorBuild_v831_"):
    if token not in load:
        errors.append("loadView must install/schedule AUv2 OOP placeholder: " + token)
if load.find("_scheduleDeferredAuv2EditorBuild_v831_") > load.find("} else if(!_built)"):
    errors.append("deferred AUv2 schedule must run before the synchronous build branch")

finish = body("-(void)_finishDeferredAuv2EditorBuild_v831_", "-(void)_scheduleDeferredAuv2EditorBuild_v831_")
for token in ("_editorChromeIsMounted_v831_",
              "_discardUnmountedBootstrapPanelState_v831_",
              "[self _buildUI];",
              "[self _startTimer];"):
    if token not in finish:
        errors.append("deferred finish missing token: " + token)
if "_knobs.count" in finish:
    errors.append("deferred finish must not use _knobs.count as a mounted-UI signal")

layout = body("-(void)viewDidLayout", "-(void)_removeBridgeObserver")
if "if(_built) [self _layoutMainChromeControls];" not in layout:
    errors.append("viewDidLayout must not layout/mount MAIN chrome before _built")
if "_scheduleDeferredAuv2EditorBuild_v831_" not in layout:
    errors.append("viewDidLayout must re-arm deferred AUv2 OOP build")

show = body("-(void)_showTab:(ArpSIDTab)t{", "-(void)_knobChg:")
for token in ("if(!_built && !_pMain)",
              "_scheduleDeferredAuv2EditorBuild_v831_",
              "return;"):
    if token not in show:
        errors.append("showTab must queue tab requests before deferred build: " + token)

cleanup = body("-(void)_discardUnmountedBootstrapPanelState_v831_", "-(void)_rebuildMixPanelFromModel_v824")
for token in ("_editorChromeIsMounted_v831_",
              "_pMain = _pLfo = _pFor",
              "[_knobs removeAllObjects]",
              "[_knobMap removeAllObjects]"):
    if token not in cleanup:
        errors.append("orphan cleanup missing token: " + token)

build = body("-(void)_buildUI{", "-(NSView*)_mainPanel:")
if "if(_built)return;" not in build:
    errors.append("_buildUI must only be gated by _built")
if "_knobs.count)return" in build or "_built||_knobs.count" in build:
    errors.append("_buildUI must not return just because _knobs.count is nonzero")
if "_discardUnmountedBootstrapPanelState_v831_" not in build:
    errors.append("_buildUI must discard orphan prebuilt panel state before mounting")

if errors:
    for err in errors:
        print("FAIL: " + err)
    sys.exit(1)
print("audit_logic_auv2_bootstrap_v831 PASS")
