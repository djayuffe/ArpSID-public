#import "ArpSIDAUAudioUnitFactory.h"
#import "ArpSIDAudioUnit.h"

@implementation ArpSIDAUAudioUnitFactory

- (void)beginRequestWithExtensionContext:(NSExtensionContext *)context {
    (void)context;
}

- (AUAudioUnit *)createAudioUnitWithComponentDescription:(AudioComponentDescription)desc
                                                   error:(NSError * __autoreleasing *)error {
    return [[ArpSIDAudioUnit alloc] initWithComponentDescription:desc options:0 error:error];
}

@end
