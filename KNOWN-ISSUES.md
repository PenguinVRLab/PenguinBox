# Known issues

## Line of Contact runs at 20 FPS — this is correct

Line of Contact is **capped at 20 FPS on original Xbox hardware**, in all three
resolution modes, and drops below that on the heavy-destruction maps even on a
real console. PenguinBox running it at 20 is native behaviour, not a performance
problem. Menus run at 30 because the cap is not engaged there — hardware does
the same.

**Worth reporting:** frames dropping *below* 20, especially somewhere a console
held 20.

## Line loops render with one edge missing (Vulkan)

Shapes drawn as line loops — boxes, rings, targeting reticles — can appear open
on one side. Vulkan has no line-loop primitive, and the emulation currently
draws a line strip without closing it.

This is an **upstream xemu** issue in the Vulkan renderer, not specific to this
fork, and it affects any title that draws line loops. A fix belongs upstream.

## The AppImage needs a current distribution

It bundles most libraries but links the host's glibc (built against 2.39). On an
older distribution it exits with `version 'GLIBC_2.39' not found`. Build from
source or use a newer distro. Windows is unaffected.

## Unconfirmed: minor visual details in Line of Contact

The performance paths were checked by eye and looked correct, but nobody
involved has played enough Line of Contact to be sure what *should* be on
screen. If something in the cockpit looks wrong to you, you are better placed to
judge it than we are — please say so.

The bisect switches in [QUICKSTART.md](QUICKSTART.md) will identify which path
is responsible, and that single line makes a report immediately actionable.

## USB passthrough needs device permissions on Linux

The controller will be visible but unusable if your user lacks rights on the USB
device node. A udev rule granting access is the standard fix. This is a host
permissions matter, not an emulator bug.

## Scope

Outside Steel Battalion and Line of Contact this build behaves like stock xemu —
the performance paths do not engage. For other games, use
[upstream xemu](https://xemu.app), which is better tested and more broadly
maintained.
