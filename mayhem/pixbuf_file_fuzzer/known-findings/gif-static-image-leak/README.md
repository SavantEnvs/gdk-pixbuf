# Finding: GIF static-image extraction leaks the internal animation wrapper

- **Target:** `pixbuf_file_fuzzer` (also reachable via `pixbuf_scale_fuzzer`, `stream_fuzzer`,
  and `animation_fuzzer` — same GIF code path, multi-frame GIF input).
- **Reproducer:** `repro.gif` in this directory (350x189 GIF89a, minimized by libFuzzer).
- **Detector:** LeakSanitizer (ASan build), halting.
- **Confirmed on:** `pixbuf_file_fuzzer-standalone repro.gif` — deterministic, reproduces every run.

## Cause

`gdk_pixbuf_new_from_file()` on a GIF loads through `gdk_pixbuf__gif_image_load()`
(`gdk-pixbuf/io-gif.c`), which internally builds a `GdkPixbufGifAnim` animation object even
when the caller only wants a single static image. When the first frame is decoded,
`io-gif.c`'s frame-append path calls `gdk_pixbuf_animation_get_static_image()` to hand the
caller a `GdkPixbuf`, which resolves to `gdk_pixbuf_gif_anim_iter_get_pixbuf()`
(`gdk-pixbuf/io-gif-animation.c:399-437`). That function lazily allocates and caches the
composited frame on the anim object itself:

```c
if (anim->last_frame_data == NULL)
        anim->last_frame_data = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, anim->width, anim->height);
```//gdk-pixbuf/io-gif-animation.c:424-425

The caller (`gdk_pixbuf_new_from_file`'s generic loader path) takes its own reference on that
returned pixbuf to return to its own caller, but the `GdkPixbufGifAnim` wrapper that
allocated and owns `last_frame_data` — built solely to extract this one static frame — is
never `g_object_unref()`'d once loading finishes. Every `gdk_pixbuf_new_from_file()` call on a
GIF with 2+ frames therefore leaks:
- the `GdkPixbufGifAnim` object itself (96 bytes in the ASan report — direct leak), and
- its `last_frame_data` `GdkPixbuf` buffer (`width * height * 4` bytes — indirect leak;
  264600 bytes for this 350x189 repro).

A single-frame (non-animated) GIF does not hit this: the leak requires at least one
`frames->next == NULL` transition, i.e. `context->animation->frames` growing past its first
element, which only happens for a genuinely multi-frame GIF.

## Impact

Memory leak, not memory corruption — every plain (non-animation-API) load of a multi-frame
GIF via `gdk_pixbuf_new_from_file()` / `gdk_pixbuf_new_from_file_at_scale()` /
`gdk_pixbuf_new_from_stream()` leaks one animation wrapper + one full decoded frame buffer.
In a long-running process that repeatedly loads untrusted multi-frame GIFs through these
whole-file APIs (as opposed to the dedicated `GdkPixbufAnimation` API), this is an
attacker-triggerable unbounded memory-growth DoS vector.

## Suggested upstream fix

In `gdk-pixbuf/io-gif.c`'s frame-append/"notify first frame" path (around
`gif_main_loop`/`gif_get_lzw`, the code that calls
`gdk_pixbuf_animation_get_static_image(GDK_PIXBUF_ANIMATION (context->animation))` and hands
the result to `prepared_func`), `g_object_ref()` the returned static pixbuf for the caller
(if not already done downstream) and then `g_object_unref (context->animation)` — or more
simply, once `_gdk_pixbuf_generic_image_load()` (`gdk-pixbuf-io.c`) has taken its own
reference to the frame pixbuf, drop the anim wrapper's reference so the wrapper's lifetime is
scoped to the single-frame extraction it was built for, matching the ordinary case where
`gdk_pixbuf_animation_new_from_file()` is the API actually being used (there the caller
legitimately owns the `GdkPixbufAnimation` object and its cache for the animation's whole
lifetime, which is correct — it's only the internal, load()-scoped use inside the plain-image
path that leaks it).

## Notes

Kept out of `mayhem/pixbuf_file_fuzzer/testsuite/` deliberately — seeds are replayed on every
Mayhem run; this is a crash artifact (LSan abort under our halting-sanitizer build), not a
starter seed, and per SPEC/PORTING crash/hang reproducers must not go in the replayed
testsuite. Found via `-fork=4 -ignore_crashes=1` short-duration fuzzing (not an
always-crasher — the target smoke-tests clean on empty/1-byte/random trivial inputs and
climbs coverage well past this bug across every enabled loader), so the target is left
un-guarded: this is a real, disclosable upstream defect, not a harness artifact to suppress.
