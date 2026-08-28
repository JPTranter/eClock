// clock_version.h — the eClock firmware version string shown on the syncing
// screen (bottom left).
//
// The default is the single source of truth, kept in lock-step with the git tag
// used for releases. A build can override it with -DECLOCK_VERSION="x.y.z" so
// the release workflow can inject the exact tagged version.
//
// NOTE: this is deliberately a *string*, not a version comparison — the clock
// only displays it; it does not branch on it.

#pragma once

#ifndef ECLOCK_VERSION
#define ECLOCK_VERSION "0.2.0"
#endif
