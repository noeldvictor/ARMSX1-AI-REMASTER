#import "ARMSXModule.h"
#import "AppDelegate.h"

@implementation ARMSXModule

RCT_EXPORT_MODULE();

RCT_EXPORT_METHOD(loadEmu:(NSArray<NSString *> *)args
                  resolver:(RCTPromiseResolveBlock)resolve
                  rejecter:(RCTPromiseRejectBlock)reject) {
    dispatch_async(dispatch_get_main_queue(), ^{
        AppDelegate *delegate = (AppDelegate *)[UIApplication sharedApplication].delegate;
        if (!delegate) {
            reject(@"no_delegate", @"AppDelegate unavailable", nil);
            return;
        }

        [delegate startSDLWithArgs:args ?: @[]];
        resolve(@(YES));
    });
}

@end
