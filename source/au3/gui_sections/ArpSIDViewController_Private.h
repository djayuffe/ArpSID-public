// ArpSIDViewController_Private.h — shared class-extension header.
//
// This header exposes the private instance-variable declarations so that
// ObjC++ category implementation files can access them without violating
// the single-class-extension rule. Each section file (#import this)
// implements one ArpSIDViewController category.
//
// NEVER include this from non-section .mm files except ArpSIDViewController.mm.
//
// Copyright (c) 2024 ArpSID Project. SPDX-License-Identifier: MIT
#pragma once

// Forward declarations needed before the class extension
#import "ArpSIDViewController.h"
#import "ArpSIDDSPKernelAdapter.h"
#include "ArpSIDComponentFlavor.h"
#include "ArpSIDDSPKernel.hpp"
#import "ArpSIDAUParameters.h"
#include "arpsid/gui/kit_panel_model.h"
#include "arpsid/gui/kit_step_grid.h"
#include "arpsid/gui/kit_voice_config.h"
#include "arpsid/gui/kit_assign_config.h"
#include "arpsid/gui/kit_state_blob.h"
#include "arpsid/gui/digi_panel_model.h"
#include "arpsid/gui/digi_sample_bank_v596.h"
#include "arpsid/gui/mix_panel_model.h"
#include "arpsid/gui/settings_panel_model.h"
#include "arpsid/gui/sidcore_panel_model.h"
#include "arpsid/gui/diagnostic_snapshot.h"
#include "arpsid/gui/gui_realtime_projection_v588.h"

// -- Section registry tag ------------------------------------------------// Any file implementing a section category should call:
// ARPSID_SECTION_IMPL(SectionEnum) at file scope (after #import this header)
// to register a static compile-time tag for that section.
#include "ArpSIDGuiSectionRegistry.h"
#define ARPSID_SECTION_IMPL(s) \
    namespace { static constexpr ArpSID::GUISections::Section _sectionTag = \
        ArpSID::GUISections::Section::s; (void)_sectionTag; }

// ------------------------------------------------------------------------
// NOTE: The actual @interface ArpSIDViewController() ivar block is still
// defined in ArpSIDViewController.mm (it must appear in EXACTLY one
// translation unit to satisfy the ObjC class-extension rule).
// Section category .mm files DO NOT redeclare the ivar block — they simply
// implement methods of ArpSIDViewController categories, gaining ivar access
// via ObjC's usual visibility rules (same module / same compilation unit via
// ARPSID_SECTION_INCLUDE).
//
// Pattern for a section file:
//
// // ArpSIDViewController+HiFiForensicPanel.mm
// #import "gui_sections/ArpSIDViewController_Private.h"
// ARPSID_SECTION_IMPL(HiFiForensicPanel)
// @implementation ArpSIDViewController (HiFiForensicPanel)
// - (NSView*)_hiFiPanel:(NSRect)r { ... }
// @end
//
// Pattern for the main file (replaces method body with textual include):
// -(NSView*)_hiFiPanel:(NSRect)r {
// #include "gui_sections/ArpSIDViewController_HiFiForensicPanel.mm.inc"
// }
// ------------------------------------------------------------------------