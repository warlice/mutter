# Profiling with Tracy

Mutter has optional instrumentation to work with the [Tracy] profiler.
It collects mostly the same data as Sysprof (spans, or in Sysprof terms, marks) using the same `COGL_TRACE` macros, but presents it differently, and has a lot of statistical analysis tools.

While Sysprof is good for finding where the CPU spends most time *in general*, Tracy is very well suited for analyzing what happened on an individual frame basis.
Why a particular frame took too long, if and why a particular scenario drops frames, what exactly happened while working on a frame—things like that.

![Inspecting a Tracy recording.](/uploads/975c2cf3f7b31360cf5b2bc5acadd775/image.png)

## Getting Started

### Building

To build Mutter with Tracy instrumentation, set `-Dprofiler=true -Dprofiler_tracy=true` meson flags.
It will pull the Tracy capture library as a subproject, if needed.

You will also need the Tracy UI, which you can build like so:

```
sudo dnf install gcc-c++ meson 'pkgconfig(capstone)' 'pkgconfig(xkbcommon)' 'pkgconfig(wayland-cursor)' 'pkgconfig(egl)' 'pkgconfig(wayland-egl)' 'pkgconfig(freetype2)' 'pkgconfig(dbus-1)'
git clone https://github.com/wolfpld/tracy.git
cd tracy/profiler/build/unix
make release
```

Then, launch it with `./Tracy-release`.

### Recording a Profile

Before recording, you may want to run the following commands in order to be able to capture more useful data (CPU scheduling and context switches):

```
echo -1 | sudo tee /proc/sys/kernel/perf_event_paranoid
sudo chmod -R a+rx /sys/kernel/debug
sudo chmod -R a+rx /sys/kernel/debug
```

You need to run the second command twice because the first time it fails to change permissions of one of the important subfolders.
It might give a bunch of permission errors, but that's fine.
The effect of those commands resets upon reboot, so you need to enter them again then.

Run Tracy-instrumented Mutter (on its own or as a GNOME Shell session).
In the Tracy UI you will see a dialog like this:

![Connection dialog.](/uploads/9b27fdb7818e59a580af9f1f089ae395/image.png)

The list at the bottom shows Tracy-instrumented programs that you can connect to.
It should show the running Mutter process.
Click on it to start recording a profile.

The UI will open the main view, where you will see the information being collected in real-time.

![Recording a profile.](/uploads/8385ca61b1e4f072a03781180ff9f9a1/image.png)

After doing the actions that you want to profile, click the Stop button in the dialog at the top-left.
Tracy will complain about stopping the recording prematurely, but that's expected, just dismiss that dialog.

Note that while you can record a profile even after days of uptime, this will cause some parts of the Tracy UI to go misaligned (some timestamps overflow).
Also note that if a program goes through a suspend/sleep and resume, the traces you capture from it become unreadable.
So when running a Tracy-instrumented session, you'll need to log out and back in before capturing new traces after a sleep and resume.

### Navigating the Timeline

The main Tracy view is a timeline.
The time goes left to right, and you see spans and other data displayed corresponding to when they happened in time.
The timeline is similar to Sysprof Mark Chart view, but the spans are nested like in a flamegraph, rather than always occupying the same row.

You can move around the timeline with a mouse.
Right-drag to move the view around, scroll up to zoom in and down to zoom out.
Tracy doesn't properly support touchpad events at the moment, so with a touchpad it will be a bit annoying, since swipes will be treated as multiple mouse scroll events.

Now you can zoom in and navigate to some area of interest (e.g. some place where you see long spans) and inspect it.

If you see lots of annoying gray regions, go into Options and disable "Draw ghost zones":

![Disabling ghost zones.](/uploads/9bfc678a840bf55cce59502ffa1a2b8f/image.png)

You can mouse over the spans to see the source location and how long they took.
You can also left-click on a span, then click Statistics to see a time distribution histogram.

![Statistics window.](/uploads/36c2f424aa69fc702c0d94413e2dc9a2/image.png)

The statistics window also shows all individual appearances of that span, which you can sort by execution time to find big outliers.
Right-clicking on one of them will bring the timeline view to that span, so you can analyze it.

For further info, please consult the excellently written and very extensive Tracy manual: https://github.com/wolfpld/tracy/releases/latest/download/tracy.pdf

[Tracy]: https://github.com/wolfpld/tracy
