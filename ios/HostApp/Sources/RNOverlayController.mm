#import "RNOverlayController.h"

#import <React/RCTBridge.h>
#import <React/RCTBundleURLProvider.h>
#import <React/RCTRootView.h>

@interface RNOverlayController () <RCTBridgeDelegate>
@property (nonatomic, strong) NSDictionary *launchOptions;
@property (nonatomic, strong, nullable) RCTBridge *bridge;
@property (nonatomic, strong, nullable) RCTRootView *rootView;
@end

@implementation RNOverlayController

- (instancetype)initWithLaunchOptions:(NSDictionary *)launchOptions {
    self = [super initWithNibName:nil bundle:nil];
    if (self) {
        _launchOptions = launchOptions ?: @{};
    }
    return self;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor blackColor];
    [self mountOverlayIfNeeded];
}

- (void)mountOverlayIfNeeded {
    if (!self.bridge) {
        self.bridge = [[RCTBridge alloc] initWithDelegate:self launchOptions:self.launchOptions];
    }

    if (self.rootView) {
        if (!self.rootView.superview) {
            [self.view addSubview:self.rootView];
        }
        return;
    }

    RCTRootView *rootView = [[RCTRootView alloc] initWithBridge:self.bridge moduleName:@"ARMSXOverlay" initialProperties:nil];
    rootView.frame = self.view.bounds;
    rootView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    rootView.backgroundColor = [UIColor blackColor];
    [self.view addSubview:rootView];
    self.rootView = rootView;
}

- (void)unmountOverlay {
    if (self.rootView && self.rootView.superview) {
        [self.rootView removeFromSuperview];
    }
    self.rootView = nil;
}

#pragma mark - RCTBridgeDelegate

- (NSURL *)sourceURLForBridge:(RCTBridge *)bridge {
#if DEBUG
    return [[RCTBundleURLProvider sharedSettings] jsBundleURLForBundleRoot:@"index"];
#else
    return [[NSBundle mainBundle] URLForResource:@"main" withExtension:@"jsbundle"];
#endif
}

@end
