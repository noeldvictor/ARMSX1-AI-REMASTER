#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface EmulatorRunner : NSObject

@property (nonatomic, assign, readonly, getter=isRunning) BOOL running;

- (void)startWithArgs:(NSArray<NSString *> *)args;

@end

NS_ASSUME_NONNULL_END
