#import "AppDelegate.h"

#import <SDL2/SDL.h>
#include <vector>
#include <string>

#import "psxe_bridge.h"

@interface AppDelegate ()
@property (nonatomic, assign) BOOL armsxRunning;
@end

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.backgroundColor = [UIColor blackColor];

    UIViewController *controller = [UIViewController new];
    controller.view.backgroundColor = [UIColor blackColor];
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];

    [self startSDL];

    return YES;
}

- (void)startSDL {
    if (self.armsxRunning) {
        return;
    }

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

- (void)applicationWillTerminate:(UIApplication *)application {
    self.armsxRunning = NO;
}

@end
