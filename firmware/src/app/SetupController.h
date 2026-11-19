#pragma once
// First-boot setup portal and the boot retry path.
namespace kk::app {

bool startSetupPortal();
void retryBoot();
void drawSetupPortal();
void continueBootAfterStorage();

}  // namespace kk::app
