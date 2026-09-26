// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>

NS_ASSUME_NONNULL_BEGIN

/// AUv3 extension principal class. Hosts load this factory through
/// NSExtensionPrincipalClass, then ask it to create the AUAudioUnit instance.
@interface ArpSIDAUAudioUnitFactory : NSObject <AUAudioUnitFactory>
@end

NS_ASSUME_NONNULL_END
