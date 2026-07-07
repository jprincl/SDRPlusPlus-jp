#if defined(__APPLE__)

#include "MacOSPermissions.h"

#import <AVFoundation/AVFoundation.h>

#include <dispatch/dispatch.h>

namespace qmx::detail {
    namespace {
        // How long we are willing to block start() while the user answers the
        // one-time system consent dialog before treating it as a denial. The
        // dialog is driven by a separate system process, so this only ever
        // fires if the user walks away from the prompt.
        constexpr int64_t kPromptTimeoutSeconds = 120;

        MicrophonePermission mapStatus(AVAuthorizationStatus status) {
            switch (status) {
                case AVAuthorizationStatusAuthorized:   return MicrophonePermission::Granted;
                case AVAuthorizationStatusDenied:        return MicrophonePermission::Denied;
                case AVAuthorizationStatusRestricted:    return MicrophonePermission::Restricted;
                case AVAuthorizationStatusNotDetermined: return MicrophonePermission::NotDetermined;
            }
            return MicrophonePermission::NotDetermined;
        }
    }

    MicrophonePermission queryMicrophonePermission() {
        if (@available(macOS 10.14, *)) {
            return mapStatus([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]);
        }
        // Pre-10.14: audio input is not gated by TCC.
        return MicrophonePermission::Granted;
    }

    MicrophonePermission requestMicrophonePermission() {
        if (@available(macOS 10.14, *)) {
            AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
            if (status != AVAuthorizationStatusNotDetermined) {
                return mapStatus(status);
            }

            // First run for this device/binary: present the consent dialog and
            // wait for the user's decision. AVFoundation delivers the callback
            // on an internal queue (not the main queue), so blocking the caller
            // here does not deadlock the app's run loop.
            __block BOOL granted = NO;
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                     completionHandler:^(BOOL allowed) {
                granted = allowed;
                dispatch_semaphore_signal(sem);
            }];

            dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, kPromptTimeoutSeconds * NSEC_PER_SEC);
            if (dispatch_semaphore_wait(sem, deadline) != 0) {
                // Timed out waiting for a response; report the live status so a
                // late "Allow" is still picked up on the next start attempt.
                return mapStatus([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]);
            }
            return granted ? MicrophonePermission::Granted : MicrophonePermission::Denied;
        }
        // Pre-10.14: audio input is not gated by TCC.
        return MicrophonePermission::Granted;
    }
}

#endif
