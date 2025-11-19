#import "AppDelegate.h"

#import <SDL2/SDL.h>
#include <vector>
#include <string>

#import "psxe_bridge.h"

@interface AppDelegate ()
@property (nonatomic, assign) SDL_Window *sdlWindow;
@property (nonatomic, assign) BOOL psxeRunning;
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
    if (self.psxeRunning) {
        return;
    }

    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        NSLog(@"SDL_Init failed: %s", SDL_GetError());
        return;
    }

    CGSize screenSize = [UIScreen mainScreen].bounds.size;
    self.sdlWindow = SDL_CreateWindow(
        "psxe",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        (int)screenSize.width,
        (int)screenSize.height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!self.sdlWindow) {
        NSLog(@"SDL_CreateWindow failed: %s", SDL_GetError());
        return;
    }

    self.psxeRunning = YES;

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            NSMutableArray<NSString *> *nativeArgs = [NSMutableArray arrayWithObject:@"psxe"];
            NSFileManager *fm = [NSFileManager defaultManager];

            // Look for bios.bin packaged in the bundle (root or Contents/)
            NSString *biosPath = [[NSBundle mainBundle] pathForResource:@"bios" ofType:@"bin"];

            if (!biosPath) {
                biosPath = [[NSBundle mainBundle] pathForResource:@"bios" ofType:@"bin" inDirectory:@"Contents"];
            }

            if (!biosPath) {
                NSString *basePath = [NSString stringWithUTF8String:SDL_GetBasePath()];
                biosPath = [basePath stringByAppendingPathComponent:@"bios.bin"];
            }

            // If found, copy to a writable pref path to avoid any translocation issues
            if (biosPath.length && [fm fileExistsAtPath:biosPath]) {
                NSString *prefBase = [NSString stringWithUTF8String:SDL_GetPrefPath("allkern", "psxe")];
                NSString *writableBios = [prefBase stringByAppendingPathComponent:@"bios.bin"];
                NSError *copyErr = nil;

                // Clean existing copy to avoid stale/corrupt data
                if ([fm fileExistsAtPath:writableBios]) {
                    [fm removeItemAtPath:writableBios error:nil];
                }

                if ([fm copyItemAtPath:biosPath toPath:writableBios error:&copyErr]) {
                    biosPath = writableBios;
                    NSLog(@"Using bundled BIOS copied to writable path %@", biosPath);
                } else {
                    NSLog(@"Failed to copy BIOS to writable location (%@). Using bundle path. Error: %@", writableBios, copyErr);
                }
            }

            std::vector<std::string> args;
            std::vector<const char *> argv;

            for (NSString *s in nativeArgs) {
                args.emplace_back([s UTF8String]);
            }

            for (const auto &s : args)
                argv.push_back(s.c_str());

            argv.push_back(nullptr);

            external_main((int)args.size(), argv.data(), self.sdlWindow);
        }
    });
}

- (void)applicationWillTerminate:(UIApplication *)application {
    self.psxeRunning = NO;
}

@end
