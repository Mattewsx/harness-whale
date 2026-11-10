[简体中文](RANDOM-INTERACTIONS.zh_CN.md)

# Random companion invitations

In the local v0.17.0 development build, the whale occasionally invites you to
wave, pet her, offer a fish or share coffee. The room keeps its three normal
controls and the session icon remains selected by default.

Select the left interaction icon and confirm. The invited action is selected
when Interact opens; confirm again to respond. Choosing another action remains
possible. Accepted invitations use a short matching response, with the usual
interaction reward and daily quota. An invitation alone never changes affection.

Only a fully lit room during daytime can start an invitation. The first eligible
wait is randomized between 45 and 75 seconds; subsequent invitations have an
8–15 minute cooldown and avoid repeating the previous action. A manual
interaction also enforces at least eight quiet minutes before a new invitation.

For a connected computer, eligibility requires a fresh global summary proving
that every main session has no running work, pending question or unread item.
A connected older bridge without this summary cannot trigger invitations.
Offline room use remains supported.

The offer lasts 20 seconds. Ignoring it produces a silent three-second ending in
the room, then normal activity resumes. No affection is deducted, no sound plays,
and automatic events never reset the dim, screen-off or desk-display timers.
New work/attention, pairing, recording, dimming or leaving the room/interaction
flow cancels the invitation. Manual interactions retain their existing behavior
when work updates arrive; only the automatic invitation is dismissed.

The implementation reuses all four outfits' existing poses. Invitation state,
cooldowns and wording variants remain in RAM; there is no new NVS record, model
request, cloud service or protocol capability.

Validation includes synthetic global/background work, accept/ignore, independent
affection quotas, voice conflicts, power timing, millisecond rollover, all outfits,
caption clearance and repeated still-frame rendering. Physical device acceptance
remains separate from host tests and rendering previews.

## Validation scope

On 2026-11-10, the user confirmed invitation triggering, response actions and captions on image `f9227d9c20f40dcc90f882fa9f1825ec11155bb8c83815bcd828fe9d3748abf0`, with the original outfit and settings retained. Subsequent daily-summary epoch invalidation and cumulative-total fixes passed regressions, the full firmware gate and native rendering; image `56574846c903fa527f85adb3a68db64904ee422866fd21697e67f68915de93f5` has not been flashed. Ignore endings, work/recording interruptions and long-duration behavior still need dedicated device checks.
