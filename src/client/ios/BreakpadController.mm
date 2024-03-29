// Copyright (c) 2012, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#import "BreakpadController.h"

#import <UIKit/UIKit.h>
#include <asl.h>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <sys/sysctl.h>

#include <processor/scoped_ptr.h>

#pragma mark -
#pragma mark Private Methods

@interface BreakpadController ()

// Init the singleton instance.
- (id)initSingleton;

// Load a crash report and send it to the server.
- (void)sendStoredCrashReports;

// Returns when a report can be sent. |-1| means never, |0| means that a report
// can be sent immediately, a positive number is the number of seconds to wait
// before being allowed to upload a report.
- (int)sendDelay;

// Notifies that a report will be sent, and update the last sending time
// accordingly.
- (void)reportWillBeSent;

@end

#pragma mark -
#pragma mark Anonymous namespace

namespace {

// The name of the user defaults key for the last submission to the crash
// server.
NSString* const kLastSubmission = @"com.google.Breakpad.LastSubmission";

// Returns a NSString describing the current platform.
NSString* GetPlatform() {
  // Name of the system call for getting the platform.
  static const char kHwMachineSysctlName[] = "hw.machine";

  NSString* result = nil;

  size_t size = 0;
  if (sysctlbyname(kHwMachineSysctlName, NULL, &size, NULL, 0) || size == 0)
    return nil;
  google_breakpad::scoped_array<char> machine(new char[size]);
  if (sysctlbyname(kHwMachineSysctlName, machine.get(), &size, NULL, 0) == 0)
    result = [NSString stringWithUTF8String:machine.get()];
  return result;
}

}  // namespace

#pragma mark -
#pragma mark BreakpadController Implementation

@implementation BreakpadController

+ (BreakpadController*)sharedInstance {
  @synchronized(self) {
    static BreakpadController* sharedInstance_ =
        [[BreakpadController alloc] initSingleton];
    return sharedInstance_;
  }
}

- (id)init {
  return nil;
}

- (id)initSingleton {
  self = [super init];
  if (self) {
    queue_ = dispatch_queue_create("com.google.BreakpadQueue", NULL);
    configuration_ = [[[NSBundle mainBundle] infoDictionary] mutableCopy];
    enableUploads_ = NO;
    started_ = NO;
    NSString* uploadInterval =
        [configuration_ valueForKey:@BREAKPAD_REPORT_INTERVAL];
    [self setUploadInterval:[uploadInterval intValue]];
  }
  return self;
}

// Since this class is a singleton, this method is not expected to be called.
- (void)dealloc {
  assert(!breakpadRef_);
  dispatch_release(queue_);
  [configuration_ release];
  [super dealloc];
}

#pragma mark -

- (void)start:(BOOL)onCurrentThread {
  if (started_)
    return;
  started_ = YES;
  void(^startBlock)() = ^{
      assert(!breakpadRef_);
      breakpadRef_ = BreakpadCreate(configuration_);
      if (breakpadRef_) {
        BreakpadAddUploadParameter(breakpadRef_, @"platform", GetPlatform());
      }
  };
  if (onCurrentThread)
    startBlock();
  else
    dispatch_async(queue_, startBlock);
}

- (void)stop {
  if (!started_)
    return;
  started_ = NO;
  dispatch_sync(queue_, ^{
      if (breakpadRef_) {
        BreakpadRelease(breakpadRef_);
        breakpadRef_ = NULL;
      }
  });
}

- (void)setUploadingEnabled:(BOOL)enabled {
  NSAssert(started_,
      @"The controller must be started before setUploadingEnabled is called");
  dispatch_async(queue_, ^{
      if (enabled == enableUploads_)
        return;
      if (enabled) {
        // Set this before calling doSendStoredCrashReport, because that
        // calls sendDelay, which in turn checks this flag.
        enableUploads_ = YES;
        [self sendStoredCrashReports];
      } else {
        enableUploads_ = NO;
        [NSObject cancelPreviousPerformRequestsWithTarget:self
            selector:@selector(sendStoredCrashReports)
            object:nil];
      }
  });
}

- (void)updateConfiguration:(NSDictionary*)configuration {
  NSAssert(!started_,
      @"The controller must not be started when updateConfiguration is called");
  [configuration_ addEntriesFromDictionary:configuration];
  NSString* uploadInterval =
      [configuration_ valueForKey:@BREAKPAD_REPORT_INTERVAL];
  if (uploadInterval)
    [self setUploadInterval:[uploadInterval intValue]];
}

- (void)setUploadingURL:(NSString*)url {
  NSAssert(!started_,
      @"The controller must not be started when setUploadingURL is called");
  [configuration_ setValue:url forKey:@BREAKPAD_URL];
}

- (void)setUploadInterval:(int)intervalInSeconds {
  NSAssert(!started_,
      @"The controller must not be started when setUploadInterval is called");
  [configuration_ removeObjectForKey:@BREAKPAD_REPORT_INTERVAL];
  uploadIntervalInSeconds_ = intervalInSeconds;
  if (uploadIntervalInSeconds_ < 0)
    uploadIntervalInSeconds_ = 0;
}

- (void)addUploadParameter:(NSString*)value forKey:(NSString*)key {
  NSAssert(started_,
      @"The controller must be started before addUploadParameter is called");
  dispatch_async(queue_, ^{
      if (breakpadRef_)
        BreakpadAddUploadParameter(breakpadRef_, key, value);
  });
}

- (void)removeUploadParameterForKey:(NSString*)key {
  NSAssert(started_, @"The controller must be started before "
                     "removeUploadParameterForKey is called");
  dispatch_async(queue_, ^{
      if (breakpadRef_)
        BreakpadRemoveUploadParameter(breakpadRef_, key);
  });
}

- (void)withBreakpadRef:(void(^)(BreakpadRef))callback {
  NSAssert(started_,
      @"The controller must be started before withBreakpadRef is called");
  dispatch_async(queue_, ^{
      callback(breakpadRef_);
  });
}


#pragma mark -

- (int)sendDelay {
  if (!breakpadRef_ || uploadIntervalInSeconds_ <= 0 || !enableUploads_)
    return -1;

  // To prevent overloading the crash server, crashes are not sent than one
  // report every |uploadIntervalInSeconds_|. A value in the user defaults is
  // used to keep the time of the last upload.
  NSUserDefaults *userDefaults = [NSUserDefaults standardUserDefaults];
  NSNumber *lastTimeNum = [userDefaults objectForKey:kLastSubmission];
  NSTimeInterval lastTime = lastTimeNum ? [lastTimeNum floatValue] : 0;
  NSTimeInterval spanSeconds = CFAbsoluteTimeGetCurrent() - lastTime;

  if (spanSeconds >= uploadIntervalInSeconds_)
    return 0;
  return uploadIntervalInSeconds_ - static_cast<int>(spanSeconds);
}

- (void)reportWillBeSent {
  NSUserDefaults *userDefaults = [NSUserDefaults standardUserDefaults];
  [userDefaults setObject:[NSNumber numberWithDouble:CFAbsoluteTimeGetCurrent()]
                   forKey:kLastSubmission];
  [userDefaults synchronize];
}

- (void)sendStoredCrashReports {
  dispatch_async(queue_, ^{
      if (!BreakpadHasCrashReportToUpload(breakpadRef_))
        return;

      int timeToWait = [self sendDelay];

      // Unable to ever send report.
      if (timeToWait == -1)
        return;

      // A report can be sent now.
      if (timeToWait == 0) {
        [self reportWillBeSent];
        BreakpadUploadNextReport(breakpadRef_);

        // If more reports must be sent, make sure this method is called again.
        if (BreakpadHasCrashReportToUpload(breakpadRef_))
          timeToWait = uploadIntervalInSeconds_;
      }

      // A report must be sent later.
      if (timeToWait > 0)
        [self performSelector:@selector(sendStoredCrashReports)
                   withObject:nil
                   afterDelay:timeToWait];
  });
}

@end
