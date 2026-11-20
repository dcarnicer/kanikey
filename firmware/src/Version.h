#pragma once
// Firmware version shown on the About screen. Keep in step with a release tag.
namespace kk {
#ifdef KANIKEY_DEV_FAIL_API
// FIX-20 test image (dev only): distinct from the running 0.1.0, so the OTA
// offer and the rollback record see a different version.
constexpr const char* kFirmwareVersion = "0.1.2";
#else
constexpr const char* kFirmwareVersion = "0.1.0";
#endif
}  // namespace kk
