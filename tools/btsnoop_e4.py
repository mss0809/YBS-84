#!/usr/bin/env python3
"""Extract ATT packets carrying the FreeFit E4 protocol from a btsnoop log.

The Android Bluetooth HCI snoop setting must be in its full/unfiltered mode.
Logs containing only HCI commands/events have no ACL/GATT payload to decode.
"""

from __future__ import annotations

import argparse
import struct
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator


@dataclass
class L2capFragment:
    expected: int
    cid: int
    payload: bytearray


@dataclass(frozen=True)
class E4Packet:
    record: int
    flags: int
    opcode: int
    handle: int
    value: bytes


def records(data: bytes):
    if len(data) < 16 or data[:8] != b"btsnoop\0":
        raise ValueError("not a btsnoop file")
    pos = 16
    index = 0
    while pos + 24 <= len(data):
        original, included, flags, drops = struct.unpack_from(">IIII", data, pos)
        timestamp = struct.unpack_from(">Q", data, pos + 16)[0]
        end = pos + 24 + included
        if end > len(data):
            raise ValueError(f"truncated btsnoop record {index}")
        yield index, flags, timestamp, data[pos + 24 : end]
        pos = end
        index += 1


def direction(flags: int) -> str:
    # Standard btsnoop flags: 0 = sent, 1 = received.  Android commonly uses
    # 2/3 for HCI command/event; ACL records use the low bit in the same way.
    return "device→phone" if flags & 1 else "phone→device"


def e4_packets(data: bytes) -> tuple[Counter[int], Iterator[E4Packet]]:
    """Return H4 type counts and a generator of decoded E4 ATT values."""
    counts: Counter[int] = Counter()
    decoded: list[E4Packet] = []
    fragments: dict[tuple[int, int], L2capFragment] = {}
    for index, flags, _, packet in records(data):
        if not packet:
            continue
        counts[packet[0]] += 1
        if packet[0] != 0x02 or len(packet) < 5:  # H4 ACL packet
            continue
        handle_pb, acl_length = struct.unpack_from("<HH", packet, 1)
        acl = packet[5 : 5 + acl_length]
        if len(acl) != acl_length:
            continue
        handle = handle_pb & 0x0FFF
        pb = (handle_pb >> 12) & 0x03
        key = (flags & 1, handle)
        if pb in (0, 2):
            if len(acl) < 4:
                continue
            length, cid = struct.unpack_from("<HH", acl)
            fragments[key] = L2capFragment(length, cid, bytearray(acl[4:]))
        else:
            fragment = fragments.get(key)
            if fragment is None:
                continue
            fragment.payload.extend(acl)
        fragment = fragments.get(key)
        if fragment is None or len(fragment.payload) < fragment.expected:
            continue
        att = bytes(fragment.payload[: fragment.expected])
        del fragments[key]
        if fragment.cid != 0x0004 or len(att) < 4:
            continue
        if att[0] not in (0x12, 0x52, 0x1B, 0x1D) or att[3] != 0xE4:
            continue
        decoded.append(E4Packet(index, flags, att[0], int.from_bytes(att[1:3], "little"), att[3:]))
    return counts, iter(decoded)


def emit_att(packet: E4Packet, show_data: bool) -> None:
    names = {0x12: "Write Request", 0x52: "Write Command", 0x1B: "Notification", 0x1D: "Indication"}
    value = packet.value
    if value[1:2] == b"\x52" and not show_data:
        rendered = f"{value[:14].hex(' ')} ... ({len(value) - 14} byte payload)"
    else:
        rendered = value.hex(" ")
    print(
        f"record={packet.record} {direction(packet.flags)} {names[packet.opcode]} "
        f"handle=0x{packet.handle:04X} value={rendered}"
    )


def extract_first_transfer(packets: list[E4Packet]) -> bytes:
    """Reassemble the first phone-to-device E4 52 data stream after E4 51."""
    collecting = False
    payload = bytearray()
    expected_length: int | None = None
    for packet in packets:
        value = packet.value
        if packet.flags & 1:
            continue
        if value[:3] == b"\xE4\x51\x01":
            if collecting:
                break
            collecting = True
            expected_length = int.from_bytes(value[6:10], "big") if len(value) >= 10 else None
            continue
        if collecting and value[:4] == b"\xE4\x52\x01\x02" and len(value) >= 14:
            payload.extend(value[14:])
            if expected_length is not None and len(payload) >= expected_length:
                return bytes(payload[:expected_length])
    if not collecting:
        raise ValueError("no phone-to-device E4 51 start packet found")
    if expected_length is not None:
        raise ValueError(f"transfer is incomplete: recovered {len(payload)} of {expected_length} bytes")
    return bytes(payload)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="btsnoop_hci.log path")
    parser.add_argument("--all", action="store_true", help="print full E4 52 payloads (normally only headers are printed)")
    parser.add_argument("--quiet", action="store_true", help="only print the summary/extraction result")
    parser.add_argument("--extract-payload", type=Path, metavar="FILE", help="write the first captured E4 51/E4 52 transfer payload to FILE")
    args = parser.parse_args()
    data = args.log.read_bytes()
    counts, packet_iter = e4_packets(data)
    packets = list(packet_iter)
    if not args.quiet:
        for packet in packets:
            emit_att(packet, args.all)

    if args.extract_payload:
        payload = extract_first_transfer(packets)
        args.extract_payload.write_bytes(payload)
        print(f"extracted {len(payload)} bytes, checksum=0x{sum(payload) & 0xFFFF:04X}, to {args.extract_payload}")

    types = ", ".join(f"0x{kind:02X}={count}" for kind, count in sorted(counts.items()))
    print(f"H4 packet types: {types or '(none)'}")
    if counts[0x02] == 0:
        print("No ACL packets (H4 type 0x02): this log has no GATT/ATT payload. Capture again with full/unfiltered Bluetooth HCI snoop logging.")
    elif not packets:
        print("ACL packets were present, but no ATT E4 value was found. Confirm the official app transfer occurred while logging was enabled.")


if __name__ == "__main__":
    main()
