// ArpSIDViewController+HiFiForensicPanel section interface.
// Panel builder + action methods for the HiFi and Forensic tabs.
#pragma once
#import "ArpSIDViewController_Private.h"

@interface ArpSIDViewController (HiFiForensicPanel)
- (NSView*)_hiFiPanel:(NSRect)r;
- (NSView*)_forensicPanel:(NSRect)r;
@end
