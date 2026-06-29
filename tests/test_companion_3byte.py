import pytest

from tools.d1l.companion3 import (
    APP_TO_RADIO,
    RADIO_TO_APP,
    Companion3Decoder,
    encode_frame,
)


def test_encode_radio_to_app_frame():
    assert encode_frame(b"\x01\x02", RADIO_TO_APP) == b">\x02\x00\x01\x02"


def test_decode_chunked_app_to_radio_frame():
    decoder = Companion3Decoder()
    assert decoder.feed(b"<\x03") == []
    assert decoder.feed(b"\x00abc") == [(APP_TO_RADIO, b"abc")]


def test_decoder_ignores_noise_before_header():
    decoder = Companion3Decoder()
    assert decoder.feed(b"noise>\x01\x00x") == [(RADIO_TO_APP, b"x")]


def test_decoder_rejects_oversized_frame_and_resyncs():
    decoder = Companion3Decoder(max_frame_size=4)
    assert decoder.feed(b"<\x05\x00>xx") == []
    assert decoder.dropped_frames == 1
    assert decoder.feed(b"yy>\x01\x00z") == [(RADIO_TO_APP, b"z")]


def test_encode_rejects_invalid_frame_type():
    with pytest.raises(ValueError):
        encode_frame(b"payload", b"!")
