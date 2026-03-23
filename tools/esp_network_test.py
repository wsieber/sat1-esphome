#!/usr/bin/env python3
"""
ESP Network Quality Test
========================
Runs iperf (v2.x) bandwidth tests between this host and an ESPHome device
using the native ESPHome API to trigger iperf on the device side.

The ESP uses ESP-IDF's built-in iperf component which speaks the iperf2
wire protocol but outputs results only to the ESP serial console.
We run plain iperf2 on the host side (no -y/-reportstyle flags) and
parse its human-readable output.

Modes:
  server  — Python runs iperf server, ESP runs iperf client  (ESP → host)
  client  — Python runs iperf client, ESP runs iperf server  (host → ESP)
  both    — run both directions sequentially (default)

Usage examples
--------------
  python esp_network_test.py --host 192.168.1.42
  python esp_network_test.py --host 192.168.1.42 --mode server --udp
  python esp_network_test.py --host 192.168.1.42 --duration 30

Dependencies
------------
  pip install aioesphomeapi
  # iperf v2.x must be in PATH (NOT iperf3)
  # macOS:  brew install iperf
  # Linux:  sudo apt install iperf
"""

import argparse
import asyncio
import re
import shutil
import socket
import subprocess
import sys
import time
import threading
from dataclasses import dataclass
from typing import Optional

import aioesphomeapi


# ---------------------------------------------------------------------------
# Result dataclass
# ---------------------------------------------------------------------------

@dataclass
class IperfResult:
    direction: str          # "esp_to_host" | "host_to_esp"
    protocol: str           # "TCP" | "UDP"
    duration_s: float
    bytes_transferred: int
    bits_per_second: float
    jitter_ms: Optional[float] = None
    lost_packets: Optional[int] = None
    total_packets: Optional[int] = None

    @property
    def mbps(self) -> float:
        return self.bits_per_second / 1_000_000

    @property
    def loss_pct(self) -> Optional[float]:
        if self.lost_packets is not None and self.total_packets:
            return 100.0 * self.lost_packets / self.total_packets
        return None

    def summary(self) -> str:
        arrow = "ESP → host" if self.direction == "esp_to_host" else "host → ESP"
        lines = [
            f"  Direction   : {arrow}",
            f"  Protocol    : {self.protocol}",
            f"  Duration    : {self.duration_s:.1f} s",
            f"  Transferred : {self.bytes_transferred / 1e6:.2f} MB",
            f"  Throughput  : {self.mbps:.2f} Mbps",
        ]
        if self.jitter_ms is not None:
            lines.append(f"  Jitter      : {self.jitter_ms:.3f} ms")
        if self.loss_pct is not None:
            lines.append(
                f"  Packet loss : {self.lost_packets}/{self.total_packets}"
                f" ({self.loss_pct:.1f} %)"
            )
        return "\n".join(lines)

    def quality_label(self) -> str:
        if self.mbps >= 50:   q = "Excellent"
        elif self.mbps >= 20: q = "Good"
        elif self.mbps >= 5:  q = "Fair"
        else:                 q = "Poor"
        notes = []
        if self.jitter_ms is not None and self.jitter_ms > 5:
            notes.append(f"high jitter {self.jitter_ms:.1f} ms")
        if self.loss_pct is not None and self.loss_pct > 1:
            notes.append(f"loss {self.loss_pct:.1f}%")
        return q + (f" — {', '.join(notes)}" if notes else "")


# ---------------------------------------------------------------------------
# iperf2 human-readable output parser
#
# Summary line looks like:
#   [  3]  0.0-10.1 sec   4.25 MBytes   3.54 Mbits/sec
# UDP server summary also has:
#   [  3]  0.0-10.0 sec   1.25 MBytes   1.05 Mbits/sec  0.123 ms  0/ 893 (0%)
# ---------------------------------------------------------------------------

# Matches the summary line — interval spanning from ~0 to the test duration
_SUMMARY_RE = re.compile(
    r"\[\s*\d+\]\s+"
    r"(\d+\.\d+)-\s*(\d+\.\d+)\s+sec\s+"   # interval: start - end
    r"([\d.]+)\s+(K?M?G?Bytes)\s+"          # transferred + unit
    r"([\d.]+)\s+(K?M?G?bits/sec)"          # bandwidth + unit
    r"(?:\s+([\d.]+)\s+ms\s+"               # optional: jitter
    r"(\d+)/\s*(\d+)\s+\([\d.]+%\))?"      # optional: lost/total
)

_UNIT_MULTIPLIERS = {
    "Bytes":      1,
    "KBytes":     1024,
    "MBytes":     1024**2,
    "GBytes":     1024**3,
    "bits/sec":   1,
    "Kbits/sec":  1000,
    "Mbits/sec":  1_000_000,
    "Gbits/sec":  1_000_000_000,
}


def parse_iperf_output(output: str, protocol: str, debug: bool = False) -> Optional[IperfResult]:
    """
    Parse human-readable iperf2 output and return the summary result.
    Picks the line with the longest interval span (= full-test summary).
    """
    if debug:
        print(f"  [parse] raw output:\n{output}", flush=True)

    best: Optional[re.Match] = None
    best_duration = 0.0

    for line in output.splitlines():
        m = _SUMMARY_RE.search(line)
        if not m:
            continue
        start_s, end_s = float(m.group(1)), float(m.group(2))
        duration = end_s - start_s
        if duration > best_duration:
            best_duration = duration
            best = m

    if best is None:
        return None

    start_s   = float(best.group(1))
    end_s     = float(best.group(2))
    xfer_val  = float(best.group(3))
    xfer_unit = best.group(4)
    bw_val    = float(best.group(5))
    bw_unit   = best.group(6)

    bytes_transferred = int(xfer_val * _UNIT_MULTIPLIERS.get(xfer_unit, 1))
    bps               = bw_val * _UNIT_MULTIPLIERS.get(bw_unit, 1)

    jitter = lost = total = None
    if best.group(7) is not None:
        jitter = float(best.group(7))
        lost   = int(best.group(8))
        total  = int(best.group(9))

    return IperfResult(
        direction="",
        protocol=protocol,
        duration_s=end_s - start_s,
        bytes_transferred=bytes_transferred,
        bits_per_second=bps,
        jitter_ms=jitter,
        lost_packets=lost,
        total_packets=total,
    )


# ---------------------------------------------------------------------------
# iperf2 process helpers
# ---------------------------------------------------------------------------

def _check_iperf() -> None:
    if shutil.which("iperf") is None:
        sys.exit(
            "ERROR: 'iperf' (v2.x) not found in PATH.\n"
            "  macOS:  brew install iperf\n"
            "  Linux:  sudo apt install iperf\n"
            "Note: requires iperf v2, NOT iperf3."
        )


def _get_local_ip() -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(("8.8.8.8", 80))
            return s.getsockname()[0]
        except OSError:
            return "0.0.0.0"


def run_iperf_server(
    duration: int,
    port: int,
    udp: bool,
    debug: bool = False,
    ready_event: Optional[threading.Event] = None,
) -> str:
    """
    Run iperf in server mode. Returns combined stdout+stderr output.

    Does NOT use -1: the ESP-IDF iperf client may not close the TCP
    connection with a clean FIN, which would leave a -1 server hanging
    forever waiting for the session to end before printing its summary.

    Instead we read stdout line by line, stop as soon as we see the
    summary line (matching the interval pattern), then kill the server.
    Hard timeout: esp_duration + 30 s.
    """
    cmd = ["iperf", "-s", "-p", str(port)]
    if udp:
        cmd.append("-u")

    if debug:
        print(f"  [server] cmd: {' '.join(cmd)}", flush=True)

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    # Signal ready via bind probe
    if ready_event is not None:
        def _probe() -> None:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                if proc.poll() is not None:
                    break
                try:
                    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                        s.bind(("0.0.0.0", port))
                    time.sleep(0.1)
                except OSError:
                    if debug:
                        print(f"  [server] port {port} bound, ready", flush=True)
                    ready_event.set()
                    return
            ready_event.set()

        threading.Thread(target=_probe, daemon=True).start()

    # Wait for esp_duration seconds (the ESP runs its own timed test),
    # then kill the server and collect all output at once.
    # The PC iperf server prints its summary only after the ESP disconnects,
    # which happens when the ESP's timer expires.
    if debug:
        print(f"  [server] waiting {duration} s for ESP test to complete ...", flush=True)
    try:
        stdout, stderr = proc.communicate(timeout=duration + 15)
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout, stderr = proc.communicate()

    combined = stdout + stderr
    if debug:
        print(f"  [server] stdout: {repr(stdout[:600])}", flush=True)
        print(f"  [server] stderr: {repr(stderr[:200])}", flush=True)
    return combined


def run_iperf_client(
    server_ip: str,
    duration: int,
    port: int,
    udp: bool,
    udp_bandwidth: str,
) -> str:
    """Run iperf in client mode. Returns combined stdout+stderr."""
    cmd = ["iperf", "-c", server_ip, "-p", str(port), "-t", str(duration)]
    if udp:
        cmd += ["-u", "-b", udp_bandwidth]

    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=duration + 30,
        )
        return proc.stdout + proc.stderr
    except subprocess.TimeoutExpired:
        return ""


# ---------------------------------------------------------------------------
# ESPHome API
# ---------------------------------------------------------------------------

async def connect_esphome(
    host: str,
    api_port: int,
    password: str,
) -> tuple[aioesphomeapi.APIClient, dict[str, aioesphomeapi.UserService]]:
    client = aioesphomeapi.APIClient(host, api_port, password)
    await client.connect(login=True)
    info = await client.device_info()
    print(f"  Connected to : {info.name}  ({info.model})")
    print(f"  ESPHome ver  : {info.esphome_version}")
    _, services = await client.list_entities_services()
    by_name = {svc.name: svc for svc in services}
    if by_name:
        print(f"  Actions found: {', '.join(by_name)}")
    else:
        print("  WARNING: No user-defined actions found on device.")
    return client, by_name


async def esp_start_server(client, services) -> None:
    svc = services.get("start_iperf_server")
    if svc is None:
        raise RuntimeError(f"'start_iperf_server' not found. Available: {list(services)}")
    await client.execute_service(svc, {})


async def esp_start_client(client, services, server_ip: str, duration: int = 0) -> None:
    svc = services.get("start_iperf_client")
    if svc is None:
        raise RuntimeError(f"'start_iperf_client' not found. Available: {list(services)}")
    data: dict = {"server": server_ip}
    # Pass duration if the action exposes it (newer firmware with duration variable)
    if any(arg.name == "duration" for arg in svc.args):
        data["duration"] = duration
    await client.execute_service(svc, data)


# ---------------------------------------------------------------------------
# Test runners
# ---------------------------------------------------------------------------

async def test_esp_to_host(
    client, services, local_ip: str,
    duration: int, port: int, udp: bool, debug: bool = False,
) -> Optional[IperfResult]:
    protocol = "UDP" if udp else "TCP"
    print(f"\n[ESP → host]  Starting local iperf server ({protocol}, port {port}) ...")

    loop = asyncio.get_event_loop()
    ready_event = threading.Event()

    server_task = loop.run_in_executor(
        None, run_iperf_server, duration, port, udp, debug, ready_event
    )

    print("[ESP → host]  Waiting for server to be ready ...")
    await loop.run_in_executor(None, lambda: ready_event.wait(timeout=15))

    print(f"[ESP → host]  Triggering ESP iperf client → {local_ip}:{port} (duration={duration}s) ...")
    await esp_start_client(client, services, local_ip, duration=duration)

    output = await server_task

    result = parse_iperf_output(output, protocol, debug=debug)
    if result is None:
        print("  ERROR: Could not parse iperf output.")
        print(f"  Raw: {repr(output[:400])}")
        return None
    result.direction = "esp_to_host"
    return result


async def test_host_to_esp(
    client, services, esp_host: str,
    duration: int, port: int, udp: bool, udp_bandwidth: str, debug: bool = False,
) -> Optional[IperfResult]:
    protocol = "UDP" if udp else "TCP"
    print(f"\n[host → ESP]  Triggering ESP iperf server ({protocol}, port {port}) ...")
    await esp_start_server(client, services)

    print("[host → ESP]  Waiting for ESP server to be ready ...")
    await asyncio.sleep(5.0)

    print(f"[host → ESP]  Running local iperf client → {esp_host}:{port} ...")
    loop = asyncio.get_event_loop()
    output = await loop.run_in_executor(
        None, run_iperf_client, esp_host, duration, port, udp, udp_bandwidth
    )

    result = parse_iperf_output(output, protocol, debug=debug)
    if result is None:
        print("  ERROR: Could not parse iperf output.")
        print(f"  Raw: {repr(output[:400])}")
        return None
    result.direction = "host_to_esp"
    return result


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

async def main(args: argparse.Namespace) -> None:
    _check_iperf()
    local_ip = args.local_ip or _get_local_ip()
    protocol = "UDP" if args.udp else "TCP"

    print("=" * 56)
    print("  ESP Network Quality Test")
    print("=" * 56)
    print(f"  Local IP     : {local_ip}")
    print(f"  ESP host     : {args.host}:{args.api_port}")
    print(f"  iperf port   : {args.port}")
    print(f"  Duration     : {args.duration} s (host→ESP) / {args.esp_duration} s (ESP→host)")
    print(f"  Protocol     : {protocol}")
    print(f"  Mode         : {args.mode}")

    print("\nConnecting to ESPHome device ...")
    try:
        client, services = await connect_esphome(args.host, args.api_port, args.password)
    except Exception as exc:
        sys.exit(f"ERROR: Could not connect: {exc}")

    results: list[IperfResult] = []
    try:
        if args.mode in ("server", "both"):
            r = await test_esp_to_host(
                client, services, local_ip,
                args.esp_duration, args.port, args.udp, args.debug,
            )
            if r:
                results.append(r)
            if args.mode == "both":
                print("\nPausing 5 s ...")
                await asyncio.sleep(5)

        if args.mode in ("client", "both"):
            r = await test_host_to_esp(
                client, services, args.host,
                args.duration, args.port, args.udp, args.udp_bandwidth, args.debug,
            )
            if r:
                results.append(r)
    finally:
        await client.disconnect()

    print("\n" + "=" * 56)
    print("  RESULTS")
    print("=" * 56)

    if not results:
        print("  No results collected.")
        return

    for r in results:
        print(f"\n--- {r.direction.replace('_', ' ').upper()} ---")
        print(r.summary())
        print(f"  Quality     : {r.quality_label()}")

    if len(results) == 2:
        total_mb  = sum(r.bytes_transferred for r in results) / 1e6
        avg_mbps  = sum(r.mbps for r in results) / 2
        print(f"\n--- AGGREGATE ---")
        print(f"  Total data     : {total_mb:.2f} MB")
        print(f"  Avg throughput : {avg_mbps:.2f} Mbps")

    print("=" * 56)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="ESP network quality test — iperf v2 + aioesphomeapi",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--host", required=True, help="ESPHome device IP or hostname")
    p.add_argument("--api-port", type=int, default=6053)
    p.add_argument("--password", default="")
    p.add_argument("--mode", choices=["server", "client", "both"], default="both")
    p.add_argument("--udp", action="store_true")
    p.add_argument("--udp-bandwidth", default="10M")
    p.add_argument("--duration", type=int, default=30,
                   help="Duration for host-side iperf client (host→ESP direction)")
    p.add_argument("--esp-duration", type=int, default=30,
                   help="Duration the ESP iperf client runs (ESP→host direction). "
                        "Must match the duration_s configured in the ESPHome iperf component.")
    p.add_argument("--port", type=int, default=5001)
    p.add_argument("--local-ip", default=None)
    p.add_argument("--debug", action="store_true", help="Print raw iperf output")
    return p.parse_args()


if __name__ == "__main__":
    asyncio.run(main(parse_args()))
