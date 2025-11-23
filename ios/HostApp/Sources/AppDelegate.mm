#import "AppDelegate.h"

#ifndef USE_HERMES
#define USE_HERMES 1
#endif
#if __has_include(<React-RCTAppDelegate/RCTAppSetupUtils.h>)
#import <React-RCTAppDelegate/RCTAppSetupUtils.h>
#else
#import <React/RCTAppSetupUtils.h>
#endif
#import "RNOverlayController.h"
#import "EmulatorRunner.h"

@interface AppDelegate ()
@property (nonatomic, strong) RNOverlayController *overlayController;
@property (nonatomic, strong) EmulatorRunner *emulatorRunner;
@end

@implementation AppDelegate

- (void)forceLandscapeIfNeeded {
    dispatch_async(dispatch_get_main_queue(), ^{
        UIInterfaceOrientation currentOrientation = UIInterfaceOrientationUnknown;
        if (@available(iOS 13.0, *)) {
            currentOrientation = self.window.windowScene.interfaceOrientation;
        } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            currentOrientation = UIApplication.sharedApplication.statusBarOrientation;
#pragma clang diagnostic pop
        }

        if (UIInterfaceOrientationIsLandscape(currentOrientation)) {
            return;
        }

        NSNumber *value = @(UIInterfaceOrientationLandscapeRight);
        [[UIDevice currentDevice] setValue:value forKey:@"orientation"];
        [UIViewController attemptRotationToDeviceOrientation];
    });
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    // Force the classic architecture so the dev tooling (DevMenu/RedBox) stays alive when Metro is
    // unavailable. This must stay in sync with the Podfile (fabric/new-arch disabled).
    BOOL enableTurboModules = NO;
    RCTAppSetupPrepareApp(application, enableTurboModules);

    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.backgroundColor = [UIColor blackColor];

    self.overlayController = [[RNOverlayController alloc] initWithLaunchOptions:launchOptions];
    self.window.rootViewController = self.overlayController;
    [self.window makeKeyAndVisible];

    [self attachReactNativeOverlay];

    return YES;
}

- (void)attachReactNativeOverlay {
    [self.overlayController mountOverlayIfNeeded];
}

- (void)teardownReactSurface {
    [self.overlayController unmountOverlay];
}

- (void)startSDLWithArgs:(NSArray<NSString *> *)args {
    [self teardownReactSurface];
    [self forceLandscapeIfNeeded];

    if (!self.emulatorRunner) {
        self.emulatorRunner = [EmulatorRunner new];
    }

    [self.emulatorRunner startWithArgs:args ?: @[]];
}

@end
