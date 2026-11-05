#pragma once

// Throwaway boot-time benchmark for the ZIM reader (build with -DZIM_SPIKE=1).
// Logs SD throughput, random-read latency, lookup and decode times over serial.
void zimSpikeRun();
