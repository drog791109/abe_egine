#!/usr/bin/env python3
"""Exercise the gateway login, lobby, game, leave, heartbeat, and wait flow.

Usage:
    python3 test/tools/abe_gateway_flow.py [options]

Parameters:
    --host HOST                  Gateway host (default: 127.0.0.1).
    --port PORT                  Gateway TCP port (default: 7000).
    --timeout SECONDS            Socket timeout (default: 3.0).
    --account ACCOUNT            Login account (default: py_flow_user).
    --nickname NICKNAME          Player nickname (default: PyFlow).
    --token TOKEN                Authentication token (default: test-auth-token).
    --device-id DEVICE_ID        Client device identifier (default: python-flow).
    --client-version VERSION     Client version (default: python-stdlib).
    --room-id ROOM_ID            Game room ID (default: 1001).
    --room-version VERSION       Game room version (default: 1).
    --leave-reason REASON        Game leave reason (default: 1).
    --heartbeat-count COUNT      Number of heartbeat requests, at least 1
                                 (default: 1).
    --heartbeat-interval-ms MS   Delay between heartbeats in milliseconds
                                 (default: 50).
    --wait-ms MS                 Time to wait for unsolicited messages in
                                 milliseconds (default: 300).

Examples:
    python3 test/tools/abe_gateway_flow.py
    python3 test/tools/abe_gateway_flow.py --host 127.0.0.1 --port 7000 \
        --account flow_user --room-id 1001 --heartbeat-count 3
"""

import argparse
import socket
import struct
import sys
import time


EXIT_OK = 0
EXIT_FAILED = 1

MAGIC = 0xABE1
VERSION = 1
PROTOCOL_HEADER_SIZE = 80

CS_PING = 10001
SC_PONG = 10002
CS_LOGIN_REQ = 11001
SC_LOGIN_RESP = 11002
CS_ENTER_LOBBY_REQ = 12001
SC_ENTER_LOBBY_RESP = 12002
CS_ENTER_GAME_REQ = 13001
SC_ENTER_GAME_RESP = 13002
CS_LEAVE_GAME_REQ = 13003
SC_LEAVE_GAME_RESP = 13099

ERROR_CODE_OK = 0
ERROR_NAMES = {
    0: "OK",
    10000: "COMMON_UNKNOWN",
    10001: "COMMON_INVALID_ARGUMENT",
    10002: "COMMON_TIMEOUT",
    10003: "COMMON_SERVER_BUSY",
    10004: "COMMON_PROTOCOL_ERROR",
    20000: "AUTH_FAILED",
    20001: "AUTH_TOKEN_EXPIRED",
    20002: "AUTH_DUPLICATE_LOGIN",
    20003: "AUTH_INVALID_ACCOUNT",
    20004: "AUTH_INVALID_NICKNAME",
    20005: "AUTH_ACCOUNT_EXISTS",
    20006: "AUTH_NICKNAME_EXISTS",
    30000: "SESSION_NOT_FOUND",
    30001: "SESSION_EXPIRED",
    30002: "SESSION_ALREADY_EXISTS",
    30003: "SESSION_NO_SLOT",
    50000: "ROOM_NOT_FOUND",
    70000: "GAME_NOT_READY",
    90000: "SYSTEM_INTERNAL",
    90001: "SYSTEM_SERVICE_UNAVAILABLE",
}


class FlowError(Exception):
    """A client-side flow assertion failure."""


def now_ms():
    return time.time_ns() // 1000000


def encode_varint(value):
    if value < 0:
        raise ValueError("protobuf varint cannot encode negative values")
    output = bytearray()
    while value > 0x7F:
        output.append((value & 0x7F) | 0x80)
        value >>= 7
    output.append(value)
    return bytes(output)


def field_varint(number, value):
    if value == 0:
        return b""
    return encode_varint(number << 3) + encode_varint(value)


def field_bytes(number, value):
    if not value:
        return b""
    return (
        encode_varint((number << 3) | 2)
        + encode_varint(len(value))
        + value
    )


def field_string(number, value):
    return field_bytes(number, value.encode("utf-8"))


def encode_message_header(protocol_id, seq, uid):
    return b"".join(
        (
            field_varint(1, protocol_id),
            field_varint(2, seq),
            field_varint(3, uid),
            field_varint(4, now_ms()),
        )
    )


def encode_room(room_id, room_version):
    return b"".join(
        (
            field_varint(1, room_id),
            field_varint(2, room_version),
        )
    )


def encode_login_request(seq, account, token, device_id, client_version, nickname):
    header = encode_message_header(CS_LOGIN_REQ, seq, 0)
    return b"".join(
        (
            field_bytes(1, header),
            field_string(2, account),
            field_string(3, token),
            field_string(4, device_id),
            field_string(5, client_version),
            field_string(6, nickname),
        )
    )


def encode_lobby_request(seq, uid):
    return b"".join(
        (
            field_bytes(1, encode_message_header(CS_ENTER_LOBBY_REQ, seq, uid)),
            field_varint(2, uid),
        )
    )


def encode_game_request(seq, uid, room_id, room_version, session_token):
    return b"".join(
        (
            field_bytes(1, encode_message_header(CS_ENTER_GAME_REQ, seq, uid)),
            field_varint(2, uid),
            field_bytes(3, encode_room(room_id, room_version)),
            field_string(4, session_token),
            field_varint(5, 1),
        )
    )


def encode_leave_request(seq, uid, room_id, room_version, reason):
    return b"".join(
        (
            field_bytes(1, encode_message_header(CS_LEAVE_GAME_REQ, seq, uid)),
            field_bytes(2, encode_room(room_id, room_version)),
            field_varint(3, reason),
        )
    )


def encode_ping(seq, uid):
    return b"".join(
        (
            field_bytes(1, encode_message_header(CS_PING, seq, uid)),
            field_varint(2, now_ms()),
        )
    )


def decode_varint(data, offset):
    value = 0
    shift = 0
    while offset < len(data):
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if (byte & 0x80) == 0:
            return value, offset
        shift += 7
        if shift >= 64:
            break
    raise FlowError("invalid protobuf varint")


def decode_fields(data):
    fields = {}
    offset = 0
    while offset < len(data):
        key, offset = decode_varint(data, offset)
        number = key >> 3
        wire_type = key & 0x07
        if number == 0:
            raise FlowError("protobuf field number is zero")

        if wire_type == 0:
            value, offset = decode_varint(data, offset)
        elif wire_type == 1:
            end = offset + 8
            if end > len(data):
                raise FlowError("truncated protobuf fixed64 field")
            value = data[offset:end]
            offset = end
        elif wire_type == 2:
            size, offset = decode_varint(data, offset)
            end = offset + size
            if end > len(data):
                raise FlowError("truncated protobuf bytes field")
            value = data[offset:end]
            offset = end
        elif wire_type == 5:
            end = offset + 4
            if end > len(data):
                raise FlowError("truncated protobuf fixed32 field")
            value = data[offset:end]
            offset = end
        else:
            raise FlowError("unsupported protobuf wire type {}".format(wire_type))
        fields.setdefault(number, []).append((wire_type, value))
    return fields


def first_field(fields, number, wire_type, default=None):
    for actual_type, value in reversed(fields.get(number, [])):
        if actual_type == wire_type:
            return value
    return default


def decode_result(fields):
    result_blob = first_field(fields, 2, 2, b"")
    result_fields = decode_fields(result_blob)
    code = first_field(result_fields, 1, 0, ERROR_CODE_OK)
    message_blob = first_field(result_fields, 2, 2, b"")
    return code, message_blob.decode("utf-8", errors="replace")


def describe_code(code):
    return "{}({})".format(ERROR_NAMES.get(code, "UNKNOWN"), code)


def parse_response(packet, expected_id, name):
    if packet["msg_id"] != expected_id:
        raise FlowError(
            "{} response id={} expected={}".format(
                name, packet["msg_id"], expected_id
            )
        )

    fields = decode_fields(packet["body"])
    header_blob = first_field(fields, 1, 2, b"")
    header_fields = decode_fields(header_blob)
    body_protocol_id = first_field(header_fields, 1, 0, 0)
    if body_protocol_id != expected_id:
        raise FlowError(
            "{} body protocol id={} expected={}".format(
                name, body_protocol_id, expected_id
            )
        )

    code, message = decode_result(fields)
    if code != ERROR_CODE_OK:
        raise FlowError(
            "{} failed with {} message={!r}".format(
                name, describe_code(code), message
            )
        )
    return fields


class GatewayClient:
    """Small standard-library client for the gateway wire format."""

    def __init__(self, host, port, timeout):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self.seq = 0

    def connect(self):
        self.sock = socket.create_connection(
            (self.host, self.port), timeout=self.timeout
        )
        self.sock.settimeout(self.timeout)

    def close(self):
        if self.sock is None:
            return
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.sock.close()
        self.sock = None

    def next_seq(self):
        self.seq += 1
        return self.seq

    def send(self, msg_id, body):
        if self.sock is None:
            raise FlowError("gateway socket is not connected")
        packet_length = PROTOCOL_HEADER_SIZE + len(body)
        header = struct.pack(
            ">HHIIQQQIIQIIIIQI",
            MAGIC,
            VERSION,
            packet_length,
            msg_id,
            0,
            0,
            0,
            self.seq,
            0,
            0,
            0,
            0,
            0,
            0,
            now_ms(),
            len(body),
        )
        packet = header + body
        self.sock.sendall(struct.pack(">I", len(packet)) + packet)

    def receive_exact(self, size):
        data = bytearray()
        while len(data) < size:
            chunk = self.sock.recv(size - len(data))
            if not chunk:
                raise FlowError("gateway closed the connection")
            data.extend(chunk)
        return bytes(data)

    def receive_packet(self, timeout=None):
        if self.sock is None:
            raise FlowError("gateway socket is not connected")
        previous_timeout = self.sock.gettimeout()
        self.sock.settimeout(self.timeout if timeout is None else timeout)
        try:
            frame_header = self.receive_exact(4)
            frame_size = struct.unpack(">I", frame_header)[0]
            if frame_size == 0 or frame_size > 65535:
                raise FlowError("invalid TCP frame size {}".format(frame_size))

            packet = self.receive_exact(frame_size)
            if len(packet) < PROTOCOL_HEADER_SIZE:
                raise FlowError("gateway packet is shorter than protocol header")

            values = struct.unpack(
                ">HHIIQQQIIQIIIIQI",
                packet[:PROTOCOL_HEADER_SIZE],
            )
            magic, version, packet_length, msg_id = values[:4]
            body_length = values[-1]
            if magic != MAGIC or version != VERSION:
                raise FlowError(
                    "invalid protocol header magic=0x{:x} version={}".format(
                        magic, version
                    )
                )
            if packet_length != len(packet):
                raise FlowError(
                    "packet length={} actual={}".format(packet_length, len(packet))
                )
            if body_length != len(packet) - PROTOCOL_HEADER_SIZE:
                raise FlowError(
                    "body length={} actual={}".format(
                        body_length, len(packet) - PROTOCOL_HEADER_SIZE
                    )
                )
            return {
                "msg_id": msg_id,
                "body": packet[PROTOCOL_HEADER_SIZE:],
            }
        finally:
            self.sock.settimeout(previous_timeout)

    def request(self, msg_id, body, expected_id, name):
        self.send(msg_id, body)
        packet = self.receive_packet()
        return parse_response(packet, expected_id, name)


def run_flow(args):
    client = GatewayClient(args.host, args.port, args.timeout)
    try:
        print("[connect] {}:{}".format(args.host, args.port))
        client.connect()

        sequence = client.next_seq()
        print("[1/6] login account={}".format(args.account))
        login_fields = client.request(
            CS_LOGIN_REQ,
            encode_login_request(
                sequence,
                args.account,
                args.token,
                args.device_id,
                args.client_version,
                args.nickname,
            ),
            SC_LOGIN_RESP,
            "login",
        )
        player_blob = first_field(login_fields, 3, 2, b"")
        player_fields = decode_fields(player_blob)
        uid = first_field(player_fields, 1, 0, 0)
        session_token_blob = first_field(login_fields, 4, 2, b"")
        session_token = session_token_blob.decode("utf-8", errors="replace")
        if uid == 0 or not session_token:
            raise FlowError("login returned empty uid or session token")
        print("      ok uid={} session_token={}...".format(uid, session_token[:8]))

        sequence = client.next_seq()
        print("[2/6] enter lobby")
        lobby_fields = client.request(
            CS_ENTER_LOBBY_REQ,
            encode_lobby_request(sequence, uid),
            SC_ENTER_LOBBY_RESP,
            "enter lobby",
        )
        lobby_time = first_field(lobby_fields, 4, 0, 0)
        print("      ok lobby_time_ms={}".format(lobby_time))

        sequence = client.next_seq()
        print("[3/6] enter game room={}".format(args.room_id))
        game_fields = client.request(
            CS_ENTER_GAME_REQ,
            encode_game_request(
                sequence,
                uid,
                args.room_id,
                args.room_version,
                session_token,
            ),
            SC_ENTER_GAME_RESP,
            "enter game",
        )
        game_start = first_field(game_fields, 4, 0, 0)
        tick_rate = first_field(game_fields, 5, 0, 0)
        print(
            "      ok game_start_time_ms={} tick_rate={}".format(
                game_start, tick_rate
            )
        )

        sequence = client.next_seq()
        print("[4/6] leave game reason={}".format(args.leave_reason))
        leave_fields = client.request(
            CS_LEAVE_GAME_REQ,
            encode_leave_request(
                sequence,
                uid,
                args.room_id,
                args.room_version,
                args.leave_reason,
            ),
            SC_LEAVE_GAME_RESP,
            "leave game",
        )
        leave_reason = first_field(leave_fields, 4, 0, 0)
        print("      ok response_reason={}".format(leave_reason))

        print("[5/6] heartbeat count={}".format(args.heartbeat_count))
        for heartbeat_index in range(args.heartbeat_count):
            sequence = client.next_seq()
            sent_at = now_ms()
            client.request(
                CS_PING,
                encode_ping(sequence, uid),
                SC_PONG,
                "heartbeat",
            )
            elapsed = now_ms() - sent_at
            print(
                "      pong {}/{} rtt_ms={}".format(
                    heartbeat_index + 1, args.heartbeat_count, elapsed
                )
            )
            if heartbeat_index + 1 < args.heartbeat_count:
                time.sleep(args.heartbeat_interval_ms / 1000.0)

        print("[6/6] wait messages={}ms".format(args.wait_ms))
        deadline = time.monotonic() + args.wait_ms / 1000.0
        received = 0
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            try:
                packet = client.receive_packet(timeout=remaining)
            except socket.timeout:
                break
            received += 1
            print("      message msg_id={} bytes={}".format(
                packet["msg_id"], len(packet["body"])
            ))
        if received == 0:
            print("      no unsolicited message")
        else:
            print("      received={}".format(received))
        print("PASS gateway flow")
        return EXIT_OK
    finally:
        client.close()


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Run the gateway login/lobby/game/leave/heartbeat flow."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7000)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--account", default="py_flow_user")
    parser.add_argument("--nickname", default="PyFlow")
    parser.add_argument("--token", default="test-auth-token")
    parser.add_argument("--device-id", default="python-flow")
    parser.add_argument("--client-version", default="python-stdlib")
    parser.add_argument("--room-id", type=int, default=1001)
    parser.add_argument("--room-version", type=int, default=1)
    parser.add_argument("--leave-reason", type=int, default=1)
    parser.add_argument("--heartbeat-count", type=int, default=1)
    parser.add_argument("--heartbeat-interval-ms", type=int, default=50)
    parser.add_argument("--wait-ms", type=int, default=300)
    args = parser.parse_args(argv)
    if args.port < 1 or args.port > 65535:
        parser.error("--port must be between 1 and 65535")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.heartbeat_count < 1:
        parser.error("--heartbeat-count must be at least 1")
    if args.wait_ms < 0 or args.heartbeat_interval_ms < 0:
        parser.error("wait and heartbeat intervals cannot be negative")
    if args.room_id < 1 or args.room_version < 0:
        parser.error("room id must be positive and room version cannot be negative")
    return args


def main(argv=None):
    try:
        return run_flow(parse_args(argv))
    except (FlowError, OSError, ValueError) as error:
        print("FAIL gateway flow: {}".format(error), file=sys.stderr)
        return EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main())
