#!/usr/bin/env python3
"""Create, inspect, or upload a custom background to a ZK/Bluetrum device.

The default path reproduces the BMP/RGB565 + 4 KiB block transfer captured
from FreeFit for this device. Bluetooth writes require --send.
"""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

SERVICE_UUID = "6e40fc00-b5a3-f393-e0a9-e50e24dcca9e"
WRITE_UUID = "6e40fc20-b5a3-f393-e0a9-e50e24dcca9e"
NOTIFY_UUID = "6e40fc21-b5a3-f393-e0a9-e50e24dcca9e"
ZK_DIAL = 0xE4
QUERY_DIAL_INFO = bytes((ZK_DIAL, 0x53, 0x01, 0x00))


@dataclass(frozen=True)
class DialInfo:
    mtu: int
    width: int
    height: int
    corners: int
    rotation: int


def be16(value: int) -> bytes:
    return struct.pack(">H", value)


def be32(value: int) -> bytes:
    return struct.pack(">I", value)


def rgb565_be(image_path: Path, width: int, height: int, corners: int = 0) -> bytes:
    """Center-crop, resize, and pack an image as Android's RGB565 big-endian bytes."""
    try:
        from PIL import Image, ImageDraw
    except ImportError as exc:
        raise RuntimeError("Pillow is required: pip install -r tools/requirements.txt") from exc

    if width <= 0 or height <= 0:
        raise ValueError("width and height must be positive")
    image = Image.open(image_path).convert("RGB")
    scale = max(width / image.width, height / image.height)
    resized = image.resize((round(image.width * scale), round(image.height * scale)), Image.Resampling.LANCZOS)
    left = (resized.width - width) // 2
    top = (resized.height - height) // 2
    image = resized.crop((left, top, left + width, top + height))
    if corners:
        mask = Image.new("L", (width, height), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, width - 1, height - 1), radius=corners, fill=255)
        background = Image.new("RGB", (width, height), "black")
        background.paste(image, mask=mask)
        image = background

    raw = bytearray(width * height * 2)
    offset = 0
    for red, green, blue in image.getdata():
        pixel = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        raw[offset : offset + 2] = be16(pixel)
        offset += 2
    return bytes(raw)


def bmp_rgb565(image_path: Path, width: int = 240, height: int = 296) -> bytes:
    """Make the exact 70-byte BMP header/RGB565 layout used by FreeFit.

    Android's ``addBMP_RGB_5652`` writes RGB565 little-endian and bottom-up.
    The 240x296 default is measured from the captured official transfer, not
    from the smaller 144x178 E4 53 preview dimensions.
    """
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError("Pillow is required: pip install -r tools/requirements.txt") from exc

    if width <= 0 or height <= 0:
        raise ValueError("BMP width and height must be positive")
    image = Image.open(image_path).convert("RGB")
    scale = max(width / image.width, height / image.height)
    resized = image.resize((round(image.width * scale), round(image.height * scale)), Image.Resampling.LANCZOS)
    left = (resized.width - width) // 2
    top = (resized.height - height) // 2
    image = resized.crop((left, top, left + width, top + height))

    pixels = bytearray(width * height * 2)
    offset = 0
    # Bitmap's int pixels are top-down. addBMP_RGB_5652 visits rows bottom-up
    # and writes each 16-bit RGB565 value little-endian.
    for y in range(height - 1, -1, -1):
        for red, green, blue in (image.getpixel((x, y)) for x in range(width)):
            pixel = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            pixels[offset : offset + 2] = struct.pack("<H", pixel)
            offset += 2

    file_size = 70 + len(pixels)
    # ImgToBmpUtil.addBMPImageHeader/addBMPImageInfosHeader verbatim.
    header = b"BM" + struct.pack("<I", file_size) + b"\0\0\0\0" + struct.pack("<I", 70)
    dib = struct.pack(
        "<IIIHHIIIIIIIIII",
        56, width, height, 1, 16, 3, 0, 456, 770, 0, 0, 0xF800, 0x07E0, 0x001F, 0,
    )
    if len(header) != 14 or len(dib) != 56:
        raise AssertionError("unexpected BMP header size")
    return header + dib + bytes(pixels)


def start_packet(payload: bytes, frame_size: int, dial_type: int, text_position: int, text_colour: int) -> bytes:
    """Build the app's 20-byte E4 51 packet for its raw-RGB565 mode."""
    if not 15 <= frame_size <= 0xFFFF:
        raise ValueError("frame_size must be between 15 and 65535")
    payload_size = frame_size - 14
    packets = (len(payload) + payload_size - 1) // payload_size
    if packets > 0xFFFF:
        raise ValueError("image needs too many packets")
    return b"".join(
        (
            bytes((ZK_DIAL, 0x51, 0x01, 0x00)),
            be16(packets),
            be32(len(payload)),
            bytes((0x00,)),
            be16(payload_size),
            bytes((dial_type & 0xFF, 0x01, text_position & 0xFF, 0x00)),
            be16(text_colour),
            bytes((0x00,)),
        )
    )


def data_packets(payload: bytes, frame_size: int):
    """Yield fixed-size E4 52 packets, including the source app's zero padding/checksum."""
    chunk_size = frame_size - 14
    total = (len(payload) + chunk_size - 1) // chunk_size
    for index in range(total):
        offset = index * chunk_size
        chunk = payload[offset : offset + chunk_size].ljust(chunk_size, b"\0")
        packet_number = index + 1
        final = 1 if packet_number == total else 0
        prefix = bytearray(
            bytes((ZK_DIAL, 0x52, 0x01, 0x02))
            + be16(packet_number)
            + be32(offset)
            + bytes(((packet_number * 100) // total, final, 0x00, 0x00))
        )
        # Java sums bytes 0..14 while positions 12..14 are still zero, then writes the checksum.
        checksum = (sum(prefix) + sum(chunk)) & 0xFFFF
        prefix[12:14] = be16(checksum)
        yield bytes(prefix) + chunk


def bmp_start_packet(payload: bytes, frame_size: int, dial_type: int, text_position: int, text_colour: int) -> bytes:
    """Build the 21-byte E4 51 start packet observed in the official app."""
    payload_size = frame_size - 14
    packets_for_start = (len(payload) + payload_size - 1) // payload_size
    if not 15 <= frame_size <= 0xFFFF or packets_for_start > 0xFFFF:
        raise ValueError("invalid frame size or payload is too large")
    return b"".join(
        (
            bytes((ZK_DIAL, 0x51, 0x01, 0x00)),
            be16(packets_for_start),
            be32(len(payload)),
            bytes((0x00,)),
            be16(payload_size),
            bytes((dial_type & 0xFF, 0x01, text_position & 0xFF, 0x00)),
            be16(text_colour),
            be16(sum(payload) & 0xFFFF),
        )
    )


def bmp_data_blocks(payload: bytes, frame_size: int):
    """Yield 4 KiB E4 52 batches for the DEVICE_MODE_DIAL nonzero path."""
    chunk_size = frame_size - 14
    if chunk_size <= 0:
        raise ValueError("frame size must be at least 15")
    packets_per_full_block = (4096 + chunk_size - 1) // chunk_size
    block_count = (len(payload) + 4095) // 4096
    total_packets = sum(
        (len(payload[block * 4096 : (block + 1) * 4096]) + chunk_size - 1) // chunk_size
        for block in range(block_count)
    )
    running_checksum = 0
    for block_index in range(block_count):
        block = payload[block_index * 4096 : (block_index + 1) * 4096]
        packets: list[bytes] = []
        for local_index, offset in enumerate(range(0, len(block), chunk_size), 1):
            chunk = block[offset : offset + chunk_size]
            number = block_index * packets_per_full_block + local_index
            running_checksum = (running_checksum + sum(chunk)) & 0xFFFF
            header = bytearray(
                bytes((ZK_DIAL, 0x52, 0x01, 0x02))
                + be16(number)
                + be32(offset)
                + bytes(((number * 100) // total_packets, 0x00))
                + be16(running_checksum)
            )
            packets.append(bytes(header) + chunk)
        yield packets


def parse_dial_info(value: bytes) -> DialInfo:
    if len(value) < 19 or value[0:2] != bytes((ZK_DIAL, 0x53)):
        raise ValueError("not an E4 53 dial-info notification")
    rotation_code = value[19] if len(value) > 19 else 0
    rotation = {0: 0, 1: 90, 2: 180, 3: 270}.get(rotation_code, 0)
    return DialInfo(
        mtu=int.from_bytes(value[11:13], "big"),
        width=int.from_bytes(value[13:15], "big"),
        height=int.from_bytes(value[15:17], "big"),
        corners=int.from_bytes(value[17:19], "big"),
        rotation=rotation,
    )


async def find_device(address: Optional[str], name: Optional[str]):
    from bleak import BleakScanner

    if address:
        return address
    if not name:
        raise ValueError("provide --address, or use --scan / --name")
    devices = await BleakScanner.discover(timeout=8)
    matches = [device for device in devices if device.name and name.casefold() in device.name.casefold()]
    if not matches:
        raise RuntimeError(f"no BLE device name containing {name!r} found")
    if len(matches) > 1:
        raise RuntimeError("multiple matches: " + ", ".join(f"{d.name} ({d.address})" for d in matches))
    return matches[0].address


async def scan() -> None:
    from bleak import BleakScanner

    for device in await BleakScanner.discover(timeout=8):
        # RSSI is not exposed on every Bleak backend/version, so keep scan portable.
        print(f"{device.address}\t{device.name or '-'}")


async def connect_and_query(address: str) -> tuple[object, asyncio.Queue[bytes], DialInfo]:
    from bleak import BleakClient

    queue: asyncio.Queue[bytes] = asyncio.Queue()
    client = BleakClient(address)
    await client.connect()

    def on_notify(_: int, value: bytearray) -> None:
        packet = bytes(value)
        print("notify:", packet.hex(" "))
        queue.put_nowait(packet)

    await client.start_notify(NOTIFY_UUID, on_notify)
    # This characteristic advertises Write Without Response only.  Windows
    # rejects an ATT Write Request with Protocol Error 0x03 (Write Not
    # Permitted), so every protocol value on this characteristic must be sent
    # as a Write Command.  Device-level E4 acknowledgements provide the
    # sequencing/reliability that ATT write responses would otherwise provide.
    await client.write_gatt_char(WRITE_UUID, QUERY_DIAL_INFO, response=False)
    while True:
        value = await asyncio.wait_for(queue.get(), timeout=8)
        if value[:2] == bytes((ZK_DIAL, 0x53)):
            return client, queue, parse_dial_info(value)


async def wait_for(queue: asyncio.Queue[bytes], command: int, timeout: float = 8) -> bytes:
    while True:
        value = await asyncio.wait_for(queue.get(), timeout=timeout)
        if value[:2] == bytes((ZK_DIAL, command)):
            return value


async def inspect(address: str) -> None:
    client = None
    try:
        client, _, info = await connect_and_query(address)
        print(f"dial-info: mtu={info.mtu}, size={info.width}x{info.height}, corners={info.corners}, rotation={info.rotation}")
        char = client.services.get_characteristic(WRITE_UUID)
        stack_limit = getattr(char, "max_write_without_response_size", None)
        if stack_limit is not None:
            print(f"BLE write-without-response limit: {stack_limit} bytes")
    finally:
        if client:
            with contextlib.suppress(Exception):
                await client.disconnect()


async def upload(args: argparse.Namespace, address: str) -> None:
    client = None
    try:
        client, queue, info = await connect_and_query(address)
        if not info.mtu:
            raise RuntimeError("device did not report a usable MTU; do not guess it")
        # The Android app's reported MTU is the entire E4 52 packet size.
        # Limit it to the desktop BLE stack's negotiated write-without-response limit.
        char = client.services.get_characteristic(WRITE_UUID)
        stack_limit = getattr(char, "max_write_without_response_size", info.mtu)
        frame_size = min(info.mtu, stack_limit)
        if frame_size < 15:
            raise RuntimeError(f"negotiated BLE write size {frame_size} is too small")
        if args.payload:
            payload = Path(args.payload).read_bytes()
            source = str(args.payload)
        else:
            payload = bmp_rgb565(Path(args.image), args.bmp_width, args.bmp_height)
            source = f"{args.bmp_width}x{args.bmp_height} BMP/RGB565"
        if payload[:2] != b"BM":
            raise ValueError("the captured transfer payload must be a BMP (starts with 'BM')")
        blocks = list(bmp_data_blocks(payload, frame_size))
        packet_count = sum(len(block) for block in blocks)
        start = bmp_start_packet(payload, frame_size, args.dial_type, args.text_position, int(args.text_colour, 16))
        print(
            f"uploading {source}: {len(payload)} bytes in {packet_count} packets / "
            f"{len(blocks)} ACK blocks, frame_size={frame_size}"
        )
        await client.write_gatt_char(WRITE_UUID, start, response=False)
        reply = await wait_for(queue, 0x51)
        if len(reply) > 2 and reply[2] > 2:
            raise RuntimeError(f"device rejected start packet: {reply.hex(' ')}")
        # The device has entered its transfer UI at this point.  Allow its
        # display/flash worker to become ready before sending the first block.
        # Android's callback/write scheduling naturally inserts a small gap;
        # WinRT otherwise submits the next Write Command immediately.
        if args.start_delay:
            print(f"waiting {args.start_delay:.3f}s for transfer readiness")
            await asyncio.sleep(args.start_delay)
        sent = 0
        for block_number, block in enumerate(blocks, 1):
            for packet in block:
                sent += 1
                if args.debug and sent == 1:
                    print(f"packet 1 header: {packet[:14].hex(' ')}")
                    print(f"packet 1 sha256: {hashlib.sha256(packet).hexdigest()}")
                if args.debug:
                    print(f"sending packet {sent}/{packet_count} (block {block_number}/{len(blocks)})")
                await client.write_gatt_char(WRITE_UUID, packet, response=False)
                if args.packet_delay:
                    await asyncio.sleep(args.packet_delay)
            try:
                reply = await wait_for(queue, 0x52, timeout=args.ack_timeout)
            except TimeoutError as exc:
                raise TimeoutError(
                    f"no E4 52 ACK after block {block_number} (through packet {sent}) "
                    f"within {args.ack_timeout:g}s; header={block[-1][:14].hex(' ')}"
                ) from exc
            if len(reply) < 10 or reply[9] != 0:
                raise RuntimeError(f"device rejected block {block_number}: {reply.hex(' ')}")
            print(f"{sent}/{packet_count} (ACK block {block_number}/{len(blocks)})")
        print("upload completed; device acknowledged every 4 KiB block")
    finally:
        if client:
            with contextlib.suppress(Exception):
                await client.disconnect()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scan", action="store_true", help="list BLE devices; no writes")
    parser.add_argument("--address", help="BLE MAC/address")
    parser.add_argument("--name", help="unique part of BLE name, used instead of --address")
    parser.add_argument("--info", action="store_true", help="query E4 53 screen/MTU info; no writes beyond the query")
    parser.add_argument("--image", help="input background image, converted to the captured BMP/RGB565 format")
    parser.add_argument("--payload", help="existing BMP payload, e.g. one extracted from btsnoop; takes precedence over --image")
    parser.add_argument("--bmp-width", type=int, default=240, help="BMP canvas width (default: 240, measured from official transfer)")
    parser.add_argument("--bmp-height", type=int, default=296, help="BMP canvas height (default: 296, measured from official transfer)")
    parser.add_argument("--output", help="write the BMP transfer payload here; does not use Bluetooth")
    parser.add_argument("--send", action="store_true", help="actually change the background after info query and ACK checks")
    parser.add_argument("--dial-type", type=int, default=1, help="app's type byte (default: 1)")
    parser.add_argument("--text-position", type=int, default=5, help="app's time-text position byte (default: 5, as in the app)")
    parser.add_argument("--text-colour", default="FFFF", help="RGB565 text colour, hexadecimal (default: FFFF)")
    parser.add_argument("--start-delay", type=float, default=0.25, help="seconds to wait after E4 51 ACK before packet 1 (default: 0.25)")
    parser.add_argument("--ack-timeout", type=float, default=8.0, help="seconds to wait for each 4 KiB E4 52 ACK (default: 8)")
    parser.add_argument("--packet-delay", type=float, default=0.01, help="seconds between Write Commands within a block (default: 0.01)")
    parser.add_argument("--debug", action="store_true", help="print transfer packet diagnostics")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    if args.scan:
        asyncio.run(scan())
        return
    if args.output:
        if args.payload:
            payload = Path(args.payload).read_bytes()
        elif args.image:
            payload = bmp_rgb565(Path(args.image), args.bmp_width, args.bmp_height)
        else:
            raise SystemExit("--output requires --image or --payload")
        Path(args.output).write_bytes(payload)
        print(f"wrote {args.output} ({len(payload)} bytes, checksum=0x{sum(payload) & 0xFFFF:04X})")
        return
    if not args.info and not args.send:
        raise SystemExit("choose --scan, --info, --output, or --send")
    address = asyncio.run(find_device(args.address, args.name))
    if args.info:
        asyncio.run(inspect(address))
    if args.send:
        if not args.image and not args.payload:
            raise SystemExit("--send requires --image or --payload")
        asyncio.run(upload(args, address))


if __name__ == "__main__":
    main()
