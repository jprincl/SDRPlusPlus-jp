#pragma once

#if defined(__APPLE__)

namespace qmx::detail {
    enum class MicrophonePermission {
        Granted,      // Access authorized; capture will receive real samples.
        Denied,       // User (or a prior prompt) denied access.
        Restricted,   // Access blocked by policy (e.g. MDM/parental controls).
        NotDetermined // No decision yet and the prompt could not be resolved.
    };

    // Returns the current microphone (audio-input) authorization status without
    // prompting the user.
    MicrophonePermission queryMicrophonePermission();

    // Ensures microphone access is authorized, presenting the system consent
    // prompt if the status is still undetermined. Blocks the calling thread
    // until the user responds (or the timeout elapses). On macOS versions prior
    // to 10.14, where audio input is not gated by TCC, this reports Granted.
    MicrophonePermission requestMicrophonePermission();
}

#endif
