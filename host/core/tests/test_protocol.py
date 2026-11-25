import json
import struct
import unittest

from nexus_core.protocol import (
    AuthContext,
    Endpoint,
    Envelope,
    FrameDecoder,
    ProtocolError,
    encode_frame,
)


class ProtocolTests(unittest.TestCase):
    def envelope(self, message_type: str = "heartbeat.ping") -> Envelope:
        return Envelope.new(
            message_type,
            {"value": 1},
            source=Endpoint("simulator", "test"),
            destination=Endpoint("host", "local"),
            auth=AuthContext(),
        )

    def test_fragmented_frame_decodes_once(self) -> None:
        encoded = encode_frame(self.envelope())
        decoder = FrameDecoder()
        messages = []
        for byte in encoded:
            messages.extend(decoder.feed(bytes([byte])))
        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0].message_type, "heartbeat.ping")

    def test_coalesced_frames_decode_in_order(self) -> None:
        decoder = FrameDecoder()
        messages = decoder.feed(
            encode_frame(self.envelope("heartbeat.ping"))
            + encode_frame(self.envelope("heartbeat.pong"))
        )
        self.assertEqual([item.message_type for item in messages], ["heartbeat.ping", "heartbeat.pong"])

    def test_oversized_frame_is_rejected(self) -> None:
        decoder = FrameDecoder(max_bytes=32)
        with self.assertRaises(ProtocolError):
            decoder.feed(struct.pack(">I", 33))

    def test_duplicate_json_key_is_rejected(self) -> None:
        envelope = self.envelope()
        raw = envelope.encode().decode("utf-8")
        duplicate = raw[:-1] + ',"message_id":"' + envelope.message_id + '"}'
        with self.assertRaises(ProtocolError):
            Envelope.decode(duplicate.encode("utf-8"))

    def test_unknown_envelope_field_is_rejected(self) -> None:
        value = self.envelope().to_dict()
        value["unexpected"] = True
        with self.assertRaises(ProtocolError):
            Envelope.from_dict(value)


if __name__ == "__main__":
    unittest.main()
