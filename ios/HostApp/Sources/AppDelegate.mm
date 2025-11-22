#import "AppDelegate.h"

#import <SDL2/SDL.h>
#ifndef USE_HERMES
#define USE_HERMES 1
#endif
#if __has_include(<React-RCTAppDelegate/RCTAppSetupUtils.h>)
#import <React-RCTAppDelegate/RCTAppSetupUtils.h>
#else
#import <React/RCTAppSetupUtils.h>
#endif
#import <React/RCTBridge.h>
#import <React/RCTBundleURLProvider.h>
#import <React/RCTRootView.h>
#include <vector>
#include <string>

#import "armsx_bridge.h"

@interface AppDelegate () <RCTBridgeDelegate>
@property (nonatomic, assign) BOOL armsxRunning;
@property (nonatomic, strong) RCTBridge *reactBridge;
@property (nonatomic, strong) RCTRootView *reactRootView;
@end

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    // We deliberately keep the old architecture/turbo-modules off here to avoid missing dev modules
    // (e.g. NativeRedBox) that were spamming the logs and blank-screening the overlay.
    RCTAppSetupPrepareApp(application, NO);

    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.backgroundColor = [UIColor blackColor];

    UIViewController *controller = [UIViewController new];
    controller.view.backgroundColor = [UIColor blackColor];
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];

    [self attachReactNativeOverlay:launchOptions];

    return YES;
}

- (void)attachReactNativeOverlay:(NSDictionary *)launchOptions {
    if (!self.window) {
        return;
    }

    if (!self.reactBridge) {
        self.reactBridge = [[RCTBridge alloc] initWithDelegate:self launchOptions:launchOptions];
    }

    self.reactRootView = [[RCTRootView alloc] initWithBridge:self.reactBridge moduleName:@"ARMSXOverlay" initialProperties:nil];
    self.reactRootView.backgroundColor = [UIColor blackColor];
    self.reactRootView.frame = self.window.bounds;
    self.reactRootView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;

    UIView *targetView = self.window.rootViewController.view ?: self.window;
    [targetView addSubview:self.reactRootView];
}

- (void)teardownReactSurface {
    if (self.reactRootView && self.reactRootView.superview) {
        [self.reactRootView removeFromSuperview];
    }
    self.reactRootView = nil;
}

- (void)startSDLWithArgs:(NSArray<NSString *> *)args {
    if (self.armsxRunning) {
        return;
    }

    [self teardownReactSurface];

    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        NSLog(@"SDL_Init failed: %s", SDL_GetError());
        return;
    }

    self.armsxRunning = YES;

    // Run the SDL/armsx entry on the main thread to satisfy UIKit threading requirements
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            NSMutableArray<NSString *> *nativeArgs = [NSMutableArray arrayWithObject:@"armsx"];
            NSFileManager *fm = [NSFileManager defaultManager];

            // Look for bios.bin packaged in the bundle (root, Contents/, or resource dir)
            NSMutableArray<NSString *> *candidatePaths = [NSMutableArray array];
            NSBundle *bundle = [NSBundle mainBundle];

            NSString *bundleRoot = bundle.bundlePath;
            NSString *resourceRoot = bundle.resourcePath;

            // Common bundle layouts
            if (bundleRoot.length) {
                [candidatePaths addObject:[bundleRoot stringByAppendingPathComponent:@"bios.bin"]];
                [candidatePaths addObject:[bundleRoot stringByAppendingPathComponent:@"Contents/bios.bin"]];
            }
            if (resourceRoot.length) {
                [candidatePaths addObject:[resourceRoot stringByAppendingPathComponent:@"bios.bin"]];
            }

            // XcodeGen resources (if the optional BIOS is present)
            NSString *biosPath = [bundle pathForResource:@"bios" ofType:@"bin"];
            if (biosPath.length) {
                [candidatePaths insertObject:biosPath atIndex:0];
            }

            biosPath = nil;

            for (NSString *candidate in candidatePaths) {
                if ([fm fileExistsAtPath:candidate]) {
                    biosPath = candidate;
                    break;
                }
            }

            // As a last resort, try SDL's base path
            if (!biosPath.length) {
                char *basePathC = SDL_GetBasePath();
                if (basePathC) {
                    NSString *basePath = [NSString stringWithUTF8String:basePathC];
                    SDL_free(basePathC);
                    NSString *fallback = [basePath stringByAppendingPathComponent:@"bios.bin"];
                    if ([fm fileExistsAtPath:fallback]) {
                        biosPath = fallback;
                    }
                }
            }

            // If found, copy to a writable pref path to avoid any translocation issues
            if (biosPath.length && [fm fileExistsAtPath:biosPath]) {
                char *prefPathC = SDL_GetPrefPath("nanodata", "armsx");
                NSString *prefBase = prefPathC ? [NSString stringWithUTF8String:prefPathC] : nil;
                if (prefPathC) {
                    SDL_free(prefPathC);
                }

                NSString *writableBios = prefBase ? [prefBase stringByAppendingPathComponent:@"bios.bin"] : nil;
                NSError *copyErr = nil;

                // Clean existing copy to avoid stale/corrupt data
                if (writableBios.length && [fm fileExistsAtPath:writableBios]) {
                    [fm removeItemAtPath:writableBios error:nil];
                }

                if (writableBios.length && [fm copyItemAtPath:biosPath toPath:writableBios error:&copyErr]) {
                    biosPath = writableBios;
                    NSLog(@"Using bundled BIOS copied to writable path %@", biosPath);
                } else if (writableBios.length) {
                    NSLog(@"Failed to copy BIOS to writable location (%@). Using bundle path. Error: %@", writableBios, copyErr);
                }
            }

            if (biosPath.length && [fm fileExistsAtPath:biosPath]) {
                [nativeArgs addObject:@"--bios"];
                [nativeArgs addObject:biosPath];
                NSLog(@"Passing BIOS path to libarmsx: %@", biosPath);
            } else {
                NSLog(@"No bundled BIOS found; libarmsx will rely on user-provided settings/CLI.");
            }

            if (args.count) {
                [nativeArgs addObjectsFromArray:args];
            }

            std::vector<std::string> args;
            std::vector<const char *> argv;

            for (NSString *s in nativeArgs) {
                args.emplace_back([s UTF8String]);
            }

            for (const auto &s : args)
                argv.push_back(s.c_str());

            argv.push_back(nullptr);

            // Let the dylib create its own SDL window/renderer
            external_main((int)args.size(), argv.data(), NULL, NULL);
        }
    });
}

- (NSURL *)sourceURLForBridge:(RCTBridge *)bridge {
#if DEBUG
    return [[RCTBundleURLProvider sharedSettings] jsBundleURLForBundleRoot:@"index"];
#else
    return [[NSBundle mainBundle] URLForResource:@"main" withExtension:@"jsbundle"];
#endif
}

- (void)applicationWillTerminate:(UIApplication *)application {
    self.armsxRunning = NO;
}

@end
