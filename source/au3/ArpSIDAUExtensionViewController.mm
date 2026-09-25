#import "ArpSIDAUExtensionViewController.h"
#import "ArpSIDAudioUnit.h"

@interface ArpSIDAUExtensionViewController ()
@property (nonatomic, assign) BOOL arpsidDidAutoConnect;
@end

@implementation ArpSIDAUExtensionViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [self arpsid_tryAutoConnectUI];
}

- (void)viewDidAppear {
    [super viewDidAppear];
    [self updatePresentationContext:@"AUv3 Extension Host Presentation"];
    [self arpsid_tryAutoConnectUI];
}

- (void)setExtensionAudioUnit:(AUAudioUnit *)extensionAudioUnit {
    if (![NSThread isMainThread]) {
        __weak ArpSIDAUExtensionViewController *weakSelf = self;
        dispatch_async(dispatch_get_main_queue(), ^{
            ArpSIDAUExtensionViewController *strongSelf = weakSelf;
            if (!strongSelf) return;
            [strongSelf setExtensionAudioUnit:extensionAudioUnit];
        });
        return;
    }
    if (_extensionAudioUnit == extensionAudioUnit && self.arpsidDidAutoConnect) return;
    _extensionAudioUnit = extensionAudioUnit;
    self.arpsidDidAutoConnect = NO;
    [self arpsid_tryAutoConnectUI];
}

- (AUAudioUnit*)createAudioUnitWithComponentDescription:(AudioComponentDescription)d
                                                 error:(NSError*__autoreleasing*)e {
    ArpSIDAudioUnit* created = [[ArpSIDAudioUnit alloc] initWithComponentDescription:d options:0 error:e];
    if (created) self.extensionAudioUnit = created;
    return created;
}

- (NSArray<AUAudioUnitViewConfiguration *> *)supportedViewConfigurations:(NSArray<AUAudioUnitViewConfiguration *> *)availableViewConfigurations {
    if (availableViewConfigurations.count == 0) return @[];
    NSMutableArray<AUAudioUnitViewConfiguration *> *accepted = [NSMutableArray array];
    for (AUAudioUnitViewConfiguration *cfg in availableViewConfigurations) {
        if (!cfg) continue;
        const CGSize size = (cfg.width > 0.0 && cfg.height > 0.0) ? CGSizeMake(cfg.width, cfg.height) : self.preferredContentSize;
        if (size.width >= 640.0 && size.height >= 420.0) {
            [accepted addObject:cfg];
        }
    }
    if (accepted.count == 0 && availableViewConfigurations.firstObject) {
        [accepted addObject:availableViewConfigurations.firstObject];
    }
    return accepted;
}

- (BOOL)selectViewConfiguration:(AUAudioUnitViewConfiguration *)viewConfiguration {
    if (!viewConfiguration) return NO;
    if (![NSThread isMainThread]) {
        __block BOOL accepted = NO;
        dispatch_sync(dispatch_get_main_queue(), ^{
            accepted = [self selectViewConfiguration:viewConfiguration];
        });
        return accepted;
    }
    const CGFloat width = MAX(640.0, viewConfiguration.width);
    const CGFloat height = MAX(420.0, viewConfiguration.height);
    self.preferredContentSize = NSMakeSize(width, height);
    if (self.isViewLoaded) {
        self.view.frame = NSMakeRect(0, 0, width, height);
        [self.view setNeedsLayout:YES];
        [self.view setNeedsDisplay:YES];
    }
    [self arpsid_tryAutoConnectUI];
    return YES;
}

- (void)arpsid_tryAutoConnectUI {
    if (self.arpsidDidAutoConnect) return;
    AUAudioUnit *au = self.extensionAudioUnit ? self.extensionAudioUnit : self.audioUnit;
    if (!au || !self.isViewLoaded) return;
    [self connectAudioUnit:au];
    self.arpsidDidAutoConnect = YES;
}

@end
