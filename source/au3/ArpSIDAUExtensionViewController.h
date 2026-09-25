#pragma once

#import "ArpSIDViewController.h"

NS_ASSUME_NONNULL_BEGIN

/// Concrete principal class compiled into the .appex bundle.
/// This avoids relying on the framework class being used directly as the
/// NSExtensionPrincipalClass, which can lead to a blank/gray AUv3 view when the
/// extension loads but the host never instantiates the intended controller.
@interface ArpSIDAUExtensionViewController : ArpSIDViewController
@property (nonatomic, strong, nullable) AUAudioUnit* extensionAudioUnit;
@end

NS_ASSUME_NONNULL_END
