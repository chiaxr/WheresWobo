#pragma once

// Overlays that make Wobo's mind visible on the ground plane: its particle
// cloud and pose hypotheses, its last LiDAR scan, how far it can currently
// see, where it is heading next, and (debug) the planner's candidates. Every
// overlay degrades gracefully when the localizer offers no diagnostics.

#include <vector>

#include "raylib.h"

#include "core/messages.h"
#include "localization/localization_types.h"
#include "planning/planner.h"

namespace wobo::overlay {

void particles(const LocalizationDiagnostics& diag);
void hypotheses(const LocalizationDiagnostics& diag, float time_s);
// The scan as the robot received it, drawn from the pose it was taken at.
void scan(const RgbLidarScan& scan, const Pose2D& from, float alpha);
void range_ring(Vec2 centre, float range_m, bool interfered, float time_s);
// Dotted path for what is left of the current plan, from the true pose.
void intent(const Pose2D& from, const RelativeMotion& remaining, float time_s);
void candidates(const Pose2D& from, const std::vector<PlanCandidate>& cands);
// Kidnap targeting preview: a ring and arrow at the chosen destination.
void target_marker(const Pose2D& target, bool valid, float time_s);
void min_distance_ring(Vec2 centre, float radius, float time_s);

// Lock-on, shown where it happens. The tether runs from the real Wobo to the
// pose it believes it is at, with dashes flowing towards Wobo as the guess
// "chases" it. The capture ring around Wobo fills clockwise, from the top of
// the screen, while the guess is within the referee's tolerance and the lock
// builds. (The real tolerance is smaller than Wobo's body, so callers draw the
// ring a little wider to keep it visible.)
void tether(const Pose2D& wobo, const Pose2D& guess, float time_s);
void capture_ring(Vec2 centre, float radius, float progress, float alarm, float time_s);

}  // namespace wobo::overlay
