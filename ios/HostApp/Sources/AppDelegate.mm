#import "AppDelegate.h"
#import "EmulatorRunner.h"

@interface AppDelegate ()
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
    (void)application;
    (void)launchOptions;

    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.backgroundColor = [UIColor blackColor];
    [self.window makeKeyAndVisible];
    [self forceLandscapeIfNeeded];

    self.emulatorRunner = [EmulatorRunner new];
    [self.emulatorRunner startWithArgs:@[]];

    return YES;
}

@end
