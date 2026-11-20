#pragma once
// Firmware version shown on the About screen. Keep in step with a release tag.
// tools/release/firmware_manifest.py reads the single quoted version below and
// refuses the header if it finds more than one; dev versions use a macro.
namespace kk {
#ifdef KANIKEY_DEV_VERSION
// Dev test images only (cardputer-adv-badapi sets it to "0.1.2" for the FIX-20
// rollback run): a version distinct from the running one, so the OTA offer and
// the rollback record see a different version.
constexpr const char* kFirmwareVersion = KANIKEY_DEV_VERSION;
#else
constexpr const char* kFirmwareVersion = "0.1.0";
#endif
}  // namespace kk
