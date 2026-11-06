#pragma once

// Throwaway boot-time check of the ZIM device libraries (build with -DZIM_DEVICE_TEST=1).
// Reads real articles through ZimBlobLoader and logs timings and heap over serial (tag ZDEV).
void zimDeviceTestRun();
