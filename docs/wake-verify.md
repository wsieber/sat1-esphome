# Speech-to-text wake check (`wake_verify`)

A second opinion on every wake word detection. When microWakeWord fires, the device sends the last 1.8 s of microphone
audio to Home Assistant's speech-to-text API and checks that the transcript actually contains a wake word. A false
fire from TV dialogue is dropped without a sound, and a wake that the wrong model caught goes ahead with the word that
was really said.

It runs entirely in the firmware (`esphome/components/wake_verify/`, wired up in `config/common/wake_verify.yaml`).
There is no Home Assistant add-on or custom integration: the device calls Home Assistant's built-in
`/api/stt/<engine>` REST endpoint with a long-lived access token, using the same speech-to-text engine the Assist
pipelines use. The component is shared with the ReSpeaker XVF3800 setup (wsieber/esp32, `components/wake_verify/`);
keep the two copies identical.

## How a wake goes through it

1. **Detection.** micro_wake_word fires and `on_wake_word_detected` runs. As before, the loader's detection record,
   the sign-in and test-window gates, the web app's detection ring and wake capture all run first, so they see every
   firing, including ones the check will reject. A ringing timer and the mute switch are handled there at once.
   Everything else is handed to `wake_verify.check`.
2. **Skip list.** A word in `skip_wake_words` (Stop) passes straight through, so Stop always acts instantly.
3. **Recording.** The component keeps a rolling 1.8 s buffer (in PSRAM) of the processed channel micro_wake_word scores,
   at the same gain factor. After the detection it waits a further 200 ms (`post_roll`) so the end of the word is in
   the clip.
4. **Level.** A quiet clip is boosted to a −6 dBFS peak, by at most 32×. The Satellite1's clips peak around 200–500
   (out of 32768), so they usually get the full 32×.
5. **Transcription.** The clip goes to `POST <ha_url>/api/stt/<stt_engine>` as 16 kHz 16-bit mono PCM, from a separate
   task so the main loop keeps running. The local OpenAI-compatible engine answers in 130–340 ms.
6. **Matching.** The transcript is normalised (lower case, punctuation dropped, "ok" read as "okay") and every run of
   neighbouring words is compared with each enabled wake word, spaces removed, by edit distance. The score is
   1 − distance / length, so "Okay, Navu." scores 0.88 against Okay Nabu and "Elfrid." 0.67 against Alfred.
7. **Verdict**, with `match_threshold` 0.65:
   - **ACCEPT**: the detected word scores at least 0.65. The wake goes ahead.
   - **REROUTE**: the detected word doesn't, but another enabled wake word does. The wake goes ahead with that word,
     so Home Assistant runs that word's pipeline and the greeting matches it.
   - **REJECT**: no enabled wake word matches. Nothing happens: no remote chime, no greeting, no stop.
8. **Action.** The `start_wake` script in `config/common/voice_assistant.yaml` runs with the verdict's word. If the
   assistant is running it is stopped (with the remote stop fan-out); if an announcement is playing it is stopped
   (with the broadcast stop); otherwise the remote wake chime, the finished-speaking-detection hand-off, the greeting
   and `voice_assistant.start` run, as they used to straight from the detection.

"Enabled" means the words in the device's two wake word slots at that moment. A model compiled in but not in a slot is
neither a match nor a reroute target.

### Two models on one utterance

Two models often fire on the same speech (Alfred on "Frederick", Frederick on "Alfred"). The Satellite1 has no 2 s
double-fire guard, so this matters more here than on the XVF3800. A detection that arrives while another is waiting
for its verdict joins that check instead of starting its own: the check already weighs every enabled word, so the wake
goes ahead once, with the word that was said. The log shows `'Alfred' joins the check of 'Frederick'`.

### Stops

A wake word heard while the assistant is listening or replying, or while an announcement plays, normally stops it.
That stop also goes through the check, so a false fire in the middle of a conversation no longer cuts it off. Stop
itself skips the check, and a ringing timer is silenced by any wake word without it.

### When the check can't run

The check never makes the device deaf. The detected word goes ahead unchecked if:

- the mode is `off`, or there is no token;
- speech-to-text fails or returns an error (`'Frederick' not checked, waking anyway: …`);
- no verdict arrives within `decision_timeout` (1.5 s) (`No verdict … waking unchecked`);
- a new detection arrives while an earlier check is still running but no longer waiting (log mode, or a check that has
  already timed out).

So while Home Assistant or the speech-to-text engine is down, false fires wake the device as they did before the check.

## Modes

`wake_verify_mode`, a substitution in `config/common/wake_verify.yaml`, is `reroute`, so every Satellite1 runs the
check. A device file can override it in its own `substitutions`.

| Mode | Behaviour |
|---|---|
| `off` | No check. `on_wake` fires at once with the detected word, so the device behaves as it did before the check. |
| `log` | `on_wake` fires at once with the detected word; the check runs in the background and is only logged, as `WOULD ACCEPT`, `WOULD REROUTE` or `WOULD REJECT`. |
| `reroute` | The wake waits for the verdict, as described above. |

## Latency

About 0.4 s per wake: the 200 ms post-roll plus the round trip to speech-to-text (median 185 ms). The greeting already
takes about a second and the assistant only listens once it has finished, so in use the delay shows only as a slightly
later greeting. Stopping a reply with a wake word takes the same extra 0.4 s; Stop does not.

## Reading the log

Each check logs one line at INFO:

```
ACCEPT 'Computer' score=1.00 heard='Computer.' (235 ms, 1.8s clip, peak 696 x23.5) [1 checked: 0 rerouted, 0 rejected]
REJECT 'Alfred' score=0.33 heard='The older approach is probably.' (206 ms, 1.8s clip, peak 436 x32.0) [...]
```

- `score`: how well the **detected** word matched the transcript.
- `heard`: the transcript.
- `(… ms)`: the speech-to-text round trip only.
- `peak N xG`: the clip's loudest sample before boosting, and the boost applied.
- The counters run since boot.

## Setup

1. **Token.** In Home Assistant, open your profile → Security → Long-lived access tokens, create one, and add it to
   `config/secrets.yaml` as `ha_token`. Every build needs it, because the package is always included, even with the
   check off.
2. **Engine.** `wake_verify_stt_engine` in `config/common/wake_verify.yaml` is the speech-to-text entity to use; the
   local OpenAI-compatible engine is fastest. `ha_url` is Home Assistant's address.
3. **Mode.** `reroute` is the default; set `wake_verify_mode: log` (or `off`) in a device file's `substitutions` to
   change it for that device.
4. Flash, and watch the log for the lines above.

The component's options (clip length, post-roll, timeouts, threshold, skip list) are listed in the docstring of
`esphome/components/wake_verify/__init__.py`.

## Tuning notes (October 2026, tested on mission_control)

- **Threshold 0.65.** At 0.75, genuine wakes heard as "Okay, now Wu." and "Redruck." (both 0.67) were rejected.
  Speech-to-text often splits "Nabu" into "now" plus another word ("Okay, now who?", 0.62), and those are still
  rejected at 0.65.
- **Lower model cutoffs.** With the check catching false fires, every Satellite1 runs Alfred at 0.96 and Frederick
  and Computer (Computerv3) at 0.92 (in `config/satellite1.yaml`) instead of the published 0.98. The user's own calls
  scored 0.92–0.96, so 0.98 missed many of them; Computer's four false fires in its first night were all rejected.
- **Speech-to-text inventions.** On noise the local engine sometimes returns text that was never said
  ("Transcribe audio to text (language: en).", "是的。"). These score near zero and are rejected.
- **Rejected clips are labelled false triggers** from the real room, useful as negatives for the next training run.
