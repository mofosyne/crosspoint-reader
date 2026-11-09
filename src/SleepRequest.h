#pragma once

// Asks the main loop to put the device into deep sleep (the way the idle timeout does). Used by the
// scheduled feed sync once it is done.
void requestDeepSleep();
