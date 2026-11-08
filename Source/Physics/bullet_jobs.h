//-----------------------------------------------------------------------------
//           Name: bullet_jobs.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Bullet's parallel loops on the job system (stage B3 of
//                 docs_perf_plan.md): the level's physics world splits its
//                 collision tests, islands and integration over the workers.
//                 Config "physics_jobs" (default true) turns it off; "jobs" 0
//                 turns it off too.
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

namespace BulletJobs {

// First thing on the main thread, before any other thread can touch Bullet: Bullet
// numbers threads in the order they first ask, and its main thread must be number 0
void ClaimMainThread();

// After Jobs::Init, on the main thread: Bullet's loops go to the job system.
// False (logged) if it couldn't; the worlds then stay single-threaded
bool Install();

// Installed, with workers to run on, and not turned off: a world may be multithreaded
bool Active();

}  // namespace BulletJobs
