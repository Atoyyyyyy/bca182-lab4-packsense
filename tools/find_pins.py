#!/usr/bin/env python3
"""Watch every GPIO input pin of the STM32F407 through the debugger (ST-LINK)
and print each pin that changes level. Use it to find which pin a key, switch
or signal is really wired to, without trusting the firmware or the pin map.

    & "$env:USERPROFILE\\.platformio\\penv\\Scripts\\python.exe" tools\\find_pins.py [seconds]

The firmware keeps running: the debugger only reads the GPIO input registers
(GPIOx_IDR) about 20 times a second. Only pins configured as inputs are
reported (outputs such as the blinking LED would show up otherwise).
Unconnected inputs float and flicker on their own, so the first BASELINE_S
seconds are hands-off: any pin that changes then is marked noisy and ignored.
A copy of the output goes to logs/find_pins_<date-time>.log (not evidence).
Close nothing: the serial port is not used. Ctrl+C stops early.
"""

import datetime
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OCD = os.path.join(os.path.expanduser("~"), ".platformio", "packages", "tool-openocd")
PORTS = "ABCDEFG"
GPIO_BASE = 0x40020000
BASELINE_S = 3.0  # hands-off seconds: pins that change now are noise

# Runs inside OpenOCD (Jim Tcl): read MODER once, then IDR of every port in a
# loop and print "<ms> <port> <idr>" whenever a value changes.
TCL = r"""
proc rd {a} { return [lindex [read_memory $a 32 1] 0] }
set ports {%(ports)s}
set i 0
foreach p $ports {
    set base [expr {0x40020000 + 0x400 * $i}]
    echo "MODER $p [format 0x%%08x [rd $base]]"
    set last($p) -1
    incr i
}
set t0 [clock milliseconds]
while {[expr {[clock milliseconds] - $t0}] < %(ms)d} {
    set i 0
    foreach p $ports {
        set v [rd [expr {0x40020010 + 0x400 * $i}]]
        if {$v != $last($p)} {
            echo "IDR [expr {[clock milliseconds] - $t0}] $p [format 0x%%04x $v]"
            set last($p) $v
        }
        incr i
    }
    echo "TICK [expr {[clock milliseconds] - $t0}]"
    sleep 40
}
echo "END"
"""


def main():
    secs = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    os.makedirs(os.path.join(ROOT, "logs"), exist_ok=True)
    stamp = datetime.datetime.now().strftime("%y%m%d-%H%M%S")
    tcl_path = os.path.join(ROOT, "logs", "find_pins_%s.tcl" % stamp)
    log_path = os.path.join(ROOT, "logs", "find_pins_%s.log" % stamp)
    with open(tcl_path, "w", newline="\n") as f:
        f.write(TCL % {"ports": " ".join(PORTS), "ms": secs * 1000})
    cmd = [os.path.join(OCD, "bin", "openocd.exe"), "-s", os.path.join(OCD, "openocd", "scripts"),
           "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg",
           "-c", "init", "-f", tcl_path, "-c", "shutdown"]

    print("PIN FINDER: watching all input pins for %d s. Ctrl+C stops early." % secs)
    print("First %.0f s: HANDS OFF (learning which pins flicker by themselves)...\n" % BASELINE_S)
    noisy = set()
    told = False
    moder = {}
    last = {}
    out = open(log_path, "w", encoding="utf-8", newline="\n")
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        for line in proc.stdout:
            parts = line.split()
            if len(parts) == 3 and parts[0] == "MODER":
                moder[parts[1]] = int(parts[2], 16)
            elif len(parts) == 4 and parts[0] == "IDR":
                t_ms, port, v = int(parts[1]), parts[2], int(parts[3], 16)
                if port in last:
                    changed = (v ^ last[port]) & 0xFFFF
                    for pin in range(16):
                        if not changed & (1 << pin):
                            continue
                        if (moder.get(port, 0) >> (2 * pin)) & 3 != 0:
                            continue  # not an input
                        name = "P%s%d" % (port, pin)
                        if t_ms < BASELINE_S * 1000:
                            noisy.add(name)
                            continue
                        if name in noisy:
                            continue
                        level = "LOW " if not v & (1 << pin) else "HIGH"
                        msg = "  %6.2f s  %-5s went %s" % (t_ms / 1000.0, name, level)
                        print(msg)
                        out.write(msg + "\n")
                        out.flush()
                last[port] = v
            elif len(parts) == 2 and parts[0] == "TICK":
                if int(parts[1]) >= BASELINE_S * 1000 and not told:
                    told = True
                    msg = "  ignoring noisy pins: %s" % (", ".join(sorted(noisy)) or "none")
                    print(msg + "\n\n  NOW press the keys one at a time, about 2 s apart, holding each ~1 s.\n")
                    out.write(msg + "\n")
            elif line.startswith("END"):
                break
            elif "Error" in line:
                print("  openocd: " + line.strip())
    except KeyboardInterrupt:
        proc.terminate()
    finally:
        proc.wait()
        out.close()
        os.remove(tcl_path)
    print("\nDone. Copy in %s. Tell the agent." % os.path.relpath(log_path, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
