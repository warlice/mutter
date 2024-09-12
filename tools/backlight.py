#!/usr/bin/env python3

import argparse
import dbus

NAME = 'org.gnome.Mutter.DisplayConfig'
INTERFACE = 'org.gnome.Mutter.DisplayConfig'
OBJECT_PATH = '/org/gnome/Mutter/DisplayConfig'

PROPS_IFACE = 'org.freedesktop.DBus.Properties'


def get_display_config():
    bus = dbus.SessionBus()
    return bus.get_object(NAME, OBJECT_PATH)

def get_backlight():
    debug_control = get_display_config()
    return debug_control.Get(INTERFACE, "Backlight", dbus_interface=PROPS_IFACE)

def set_backlight(serial, connector, value):
    debug_control = get_display_config()
    debug_control.SetBacklight(dbus.UInt32(serial), connector, dbus.Int32(value), dbus_interface=INTERFACE)

def do_status():
    [serial, backlights] = get_backlight()

    print(f"Serial: {serial}")

    for backlight in backlights:
        connector = backlight["connector"]
        print(f"Connector '{connector}':")

        for prop, value in backlight.items():
            if prop == "connector":
                continue
            print(f"  {prop}: {value}")

def do_set(kv):
    debug_control = get_display_config()
    [connector, value] = kv
    [serial, _] = get_backlight()
    set_backlight(serial, connector, value)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Interact with the backlight control')

    parser.add_argument('--status', action='store_true')
    parser.add_argument('--set', metavar=('connector', 'value'), type=str, nargs=2)

    args = parser.parse_args()
    if args.status:
        do_status()
    elif args.set:
        do_set(args.set)
    else:
        parser.print_usage()
